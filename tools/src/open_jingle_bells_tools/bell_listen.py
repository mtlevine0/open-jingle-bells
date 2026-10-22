#!/usr/bin/env python3
"""Record the bells with a microphone and check what they actually played.

Plays strikes on the player over USB serial while recording through
PipeWire/PulseAudio (parecord), then detects each strike in the recording,
identifies which bells rang, and compares that with what was sent.

    # once per room/microphone placement: learn each bell's real pitch
    ojb-bell-listen calibrate
    # play and check the ROM intro scale
    ojb-bell-listen scale
    # play and check a composition (frames from a ROM header)
    ojb-bell-listen rom tools/songs/frosty_chords_rom.h --frames 64
    # re-check a saved performance (e.g. after recalibrating)
    ojb-bell-listen rescore media/recordings/<take>.json
    # list the strikes in any recording
    ojb-bell-listen analyze take.wav

Recordings and JSON reports go to media/recordings/. Needs numpy.
"""

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time
import wave
from pathlib import Path

import numpy as np

from .paths import CALIBRATION, MEDIA, PINOUT
from .play_scale import ROM, Player, rom_scale

RECORDINGS = MEDIA / "recordings"

RATE = 48000
FFT_SIZE = 4096  # 11.7 Hz bins: enough to separate the bells a semitone apart
HOP = 480  # 10 ms
BAND = 0.012  # +-1.2% around each bell's frequency
MATCH_S = 0.12  # detected strike within 120 ms of a sent strike counts as a match
MIN_SNR = 4.0  # a strike must ring at least this far above the noise floor
MIN_STRENGTH = 0.3  # a heard bell must rise at least this fraction of its calibrated strike
WEAK_RISE = 1.15  # sent bell rose this much but was not separable: "weak"


def load_bells():
    """Return [(rom_bit, note, nominal_hz)] for the 12 bells, highest first."""
    import csv

    bells = []
    for row in csv.DictReader(PINOUT.open()):
        note = row["note"]
        semitone = "C C# D D# E F F# G G# A A# B".split().index(note[:-1])
        midi = 12 * (int(note[-1]) + 1) + semitone
        bells.append((int(row["rom_bit"]), note, 440.0 * 2 ** ((midi - 69) / 12)))
    return bells


BELLS = load_bells()


def mask_notes(mask):
    return [note for bit, note, _ in BELLS if mask >> bit & 1]


# --- recording ---------------------------------------------------------------

class Recorder:
    """Mono 48 kHz WAV recording via parecord, stopped with SIGINT."""

    def __init__(self, path, device=None):
        cmd = ["parecord", "--channels=1", f"--rate={RATE}", "--format=s16le", "--file-format=wav"]
        if device:
            cmd.append(f"--device={device}")
        self.path = path
        self.proc = subprocess.Popen(cmd + [str(path)])
        # Wait until audio is actually flowing so the first strikes are captured.
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if path.exists() and path.stat().st_size > 44 + RATE // 2:
                break
            time.sleep(0.02)
        else:
            self.stop()
            raise RuntimeError("microphone produced no audio")

    def stop(self):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGINT)
            self.proc.wait(timeout=5)


def read_wav(path):
    with wave.open(str(path)) as w:
        data = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float64) / 32768
        channels, rate = w.getnchannels(), w.getframerate()
    if channels > 1:
        data = data.reshape(-1, channels).mean(axis=1)
    if rate != RATE:
        raise ValueError(f"{path}: expected {RATE} Hz, got {rate}")
    return data


def show_progress(frame, frames, elapsed_s, total_s, width=30):
    """Draw a one-line progress bar on stderr; skipped when it isn't a terminal."""
    if not sys.stderr.isatty():
        return
    done = min(1.0, elapsed_s / total_s) if total_s > 0 else 1.0
    filled = round(width * done)
    clock = lambda t: f"{int(t) // 60}:{int(t) % 60:02d}"
    sys.stderr.write(f"\r[{'#' * filled}{'-' * (width - filled)}] {done:4.0%}  "
                     f"{clock(elapsed_s)} / {clock(total_s)}  frame {frame}/{frames}")
    sys.stderr.flush()


def perform(name, strikes, step_s, port, device, pulse_ms=None, lead_s=0.6, tail_s=4.0):
    """Record while sending strikes; return (wav path, [(time_s, mask)])."""
    RECORDINGS.mkdir(parents=True, exist_ok=True)
    wav = RECORDINGS / f"{time.strftime('%Y%m%d-%H%M%S')}-{name}.wav"
    player = Player(port)
    sent = []
    try:
        player.drain()
        if pulse_ms is not None:
            player.command(f"pulse {pulse_ms}")
        recorder = Recorder(wav, device)
        start = time.monotonic()
        total_s = lead_s + len(strikes) * step_s + tail_s
        try:
            time.sleep(lead_s)
            for index, mask in enumerate(strikes):
                target = start + lead_s + index * step_s
                time.sleep(max(0, target - time.monotonic()))
                if mask:
                    sent.append((time.monotonic() - start, mask))
                    player.command(f"strike 0x{mask:03X}")
                show_progress(index + 1, len(strikes), time.monotonic() - start, total_s)
            # Keep the bar moving while the last bells ring out.
            while (left := start + total_s - time.monotonic()) > 0:
                time.sleep(min(0.25, left))
                show_progress(len(strikes), len(strikes), time.monotonic() - start, total_s)
        finally:
            recorder.stop()
            if sys.stderr.isatty():
                sys.stderr.write("\n")
    finally:
        player.close()
    return wav, sent


# --- analysis ----------------------------------------------------------------

def spectrogram(audio):
    window = np.hanning(FFT_SIZE)
    frames = np.lib.stride_tricks.sliding_window_view(audio, FFT_SIZE)[::HOP]
    spec = np.abs(np.fft.rfft(frames * window, axis=1))
    freqs = np.fft.rfftfreq(FFT_SIZE, 1 / RATE)
    times = (np.arange(len(frames)) * HOP + FFT_SIZE / 2) / RATE
    return times, freqs, spec


def band_energy(freqs, spec, centers):
    """Peak magnitude within +-BAND of each center frequency, per frame."""
    out = np.empty((spec.shape[0], len(centers)))
    for i, hz in enumerate(centers):
        sel = (freqs > hz * (1 - BAND)) & (freqs < hz * (1 + BAND))
        out[:, i] = spec[:, sel].max(axis=1)
    return out


def peak_rise(energy, hold=25, gap=2):
    """Per frame and band: log level above the band's peak over the previous
    ~250 ms (ending `gap` frames back). A fresh strike or a re-strike pushes a
    bell above its recent peak; beating and decay never do."""
    log = np.log(energy + 1e-6)
    prior = np.full_like(log, -np.inf)
    for k in range(gap, gap + hold):
        prior[k:] = np.maximum(prior[k:], log[:-k])
    rise = log - prior
    rise[:gap + 1] = 0
    return rise


def onsets(times, energy, min_gap_s=0.12, min_rise=0.35, min_snr=MIN_SNR):
    """Frames where some bell band rises above its recent peak and well above
    its noise floor. Returns the frame of the strongest rise in each burst."""
    log = np.log(energy + 1e-6)
    floor = np.log(np.median(energy, axis=0) + 1e-6)
    rise = peak_rise(energy) * ((log - floor) > np.log(min_snr))
    score = rise.max(axis=1)
    peaks = []
    for i in range(1, len(score) - 1):
        if score[i] >= min_rise and score[i] >= score[i - 1] and score[i] > score[i + 1]:
            if peaks and times[i] - times[peaks[-1]] < min_gap_s:
                if score[i] > score[peaks[-1]]:
                    peaks[-1] = i
                continue
            peaks.append(i)
    return peaks


def align(times, energy, sent, max_latency_s=5.0):
    """Recording latency that best lines the sent strikes up with band rises.
    energy has one column per bell (BELLS order)."""
    log = np.log(energy + 1e-6)
    rise = np.zeros_like(log)
    rise[6:] = log[6:] - log[:-6]
    columns = {bit: i for i, (bit, _, _) in enumerate(BELLS)}
    step = times[1] - times[0]
    best = (-np.inf, 0.0)
    for offset in np.arange(0, max_latency_s, step):
        total = 0.0
        for ts, mask in sent:
            frame = int(round((ts + offset - times[0]) / step))
            if frame + 6 >= len(rise):
                break
            cols = [columns[bit] for bit in columns if mask >> bit & 1]
            total += rise[frame:frame + 6, cols].max()
        if total > best[0]:
            best = (total, offset)
    return best[1]


def strike_vector(energy, frame, rise, hold=25, gap=2):
    """How much each band rose at a strike, measured from its level just before.
    Bands that stay below their recent peak (still ringing or beating from an
    earlier strike) don't count."""
    before = energy[max(0, frame - 8):max(1, frame - gap)].min(axis=0)
    after = energy[frame:frame + 6].max(axis=0)
    struck = rise[frame:frame + 6].max(axis=0) > -0.05
    return np.clip(after - before, 0, None) * struck


def nnls(a, b, iterations=200):
    """Non-negative least squares min |a x - b|, x >= 0 (projected gradient)."""
    x = np.zeros(a.shape[1])
    step = 1 / (np.linalg.norm(a, 2) ** 2 + 1e-12)
    for _ in range(iterations):
        x = np.clip(x - step * (a.T @ (a @ x - b)), 0, None)
    return x


def detect(audio, calibration):
    """Return ([(time_s, mask)] of strikes heard, frame times, band energy)."""
    times, freqs, spec = spectrogram(audio)
    centers = [calibration["bells"][str(bit)]["hz"] for bit, _, _ in BELLS]
    templates = np.array([calibration["bells"][str(bit)]["template"] for bit, _, _ in BELLS]).T
    energy = band_energy(freqs, spec, centers)
    rise = peak_rise(energy)
    strength = np.array([calibration["bells"][str(bit)]["strength"] for bit, _, _ in BELLS])
    found = []
    for frame in onsets(times, energy):
        vec = strike_vector(energy, frame, rise)
        weights = nnls(templates, vec)
        if weights.max() <= 0:
            continue
        # Judge each bell against its own calibrated strike, so a loud bell
        # (F6 is ~5x D7 at the mic) doesn't hide quieter bells in a chord.
        relative = weights / strength
        mask = 0
        for i, (bit, _, _) in enumerate(BELLS):
            if relative[i] > 0.35 * relative.max() and relative[i] > MIN_STRENGTH:
                mask |= 1 << bit
        if mask:
            found.append((float(times[frame]), mask))
    return found, times, energy


def compare(sent, heard, offset):
    """Match heard strikes to sent ones, given the recording latency."""
    rows, used = [], set()
    for ts, mask in sent:
        # Prefer the nearby heard strike that shares the most bells with this one.
        candidates = [j for j, (th, _) in enumerate(heard)
                      if j not in used and abs(th - (ts + offset)) <= MATCH_S]
        if not candidates:
            rows.append((ts, mask, None, None))
            continue
        match = max(candidates, key=lambda j: ((heard[j][1] & mask).bit_count(),
                                               -abs(heard[j][0] - (ts + offset))))
        used.add(match)
        rows.append((ts, mask, heard[match][0] - offset, heard[match][1]))
    # Only count extras during the performance, not mic start-up noise.
    first, last = sent[0][0] - 0.3, sent[-1][0] + 1.5
    extra = [(th - offset, hmask) for j, (th, hmask) in enumerate(heard)
             if j not in used and first <= th - offset <= last]
    return rows, extra


def sounded(times, energy, t, mask):
    """True if every bell in mask got louder around time t, even if the strike
    could not be told apart from the bell still ringing."""
    frame = int(np.searchsorted(times, t))
    for i, (bit, _, _) in enumerate(BELLS):
        if mask >> bit & 1:
            before = energy[max(0, frame - 10):max(1, frame - 1), i].min()
            after = energy[max(0, frame - 3):frame + 8, i].max()
            if after < WEAK_RISE * before:
                return False
    return True


def report(sent, heard, offset, times, energy, wav):
    rows, extra = compare(sent, heard, offset)
    counts = {"ok": 0, "weak": 0, "WRONG": 0, "MISSED": 0}
    strikes = []
    print(f"\n{'sent s':>7}  {'sent':<16} {'heard':<16} result")
    for ts, mask, th, hmask in rows:
        if hmask == mask:
            verdict = "ok"
        elif sounded(times, energy, ts + offset, mask):
            verdict = "weak"
        else:
            verdict = "MISSED" if th is None else "WRONG"
        counts[verdict] += 1
        heard_txt = " ".join(mask_notes(hmask)) if hmask else "-"
        timing = f"  ({(th - ts) * 1000:+.0f} ms)" if th is not None else ""
        print(f"{ts:7.2f}  {' '.join(mask_notes(mask)):<16} {heard_txt:<16} {verdict}{timing}")
        strikes.append({"sent_s": ts, "sent": mask_notes(mask), "heard_s": th,
                        "heard": mask_notes(hmask) if hmask else None, "result": verdict})
    for th, hmask in extra:
        print(f"{th:7.2f}  {'-':<16} {' '.join(mask_notes(hmask)):<16} EXTRA")
    total = len(rows)
    print(f"\n{counts['ok']}/{total} strikes heard correctly, {counts['weak']} weak "
          f"(bell sounded but blurred with its own ringing), {counts['WRONG']} wrong, "
          f"{counts['MISSED']} missed, {len(extra)} extra; recording latency {offset * 1000:.0f} ms")
    result = {"recording": str(wav), "latency_s": offset, "total": total, **counts,
              "extra_count": len(extra), "strikes": strikes,
              "extra": [{"heard_s": th, "heard": mask_notes(hmask)} for th, hmask in extra]}
    wav.with_suffix(".json").write_text(json.dumps(result, indent=2))
    print(f"recording: {wav}\nreport:    {wav.with_suffix('.json')}")
    return counts["ok"] + counts["weak"] == total and not extra


# --- calibration ---------------------------------------------------------------

def calibrate(args):
    gap = 1.0
    order = [bit for bit, _, _ in BELLS] * args.repeats
    wav, sent = perform("calibrate", [1 << bit for bit in order], gap, args.port, args.device)
    audio = read_wav(wav)
    times, freqs, spec = spectrogram(audio)
    nominal = [hz for _, _, hz in BELLS]

    # Measure each bell's real pitch from its own strikes, searching +-5% of
    # nominal. Latency is found by lining the strike pattern up with the audio.
    wide = np.empty((spec.shape[0], len(BELLS)))
    for i, hz in enumerate(nominal):
        wide[:, i] = spec[:, (freqs > hz * 0.95) & (freqs < hz * 1.05)].max(axis=1)
    offset = align(times, wide, sent)

    measured = {}
    for ts, mask in sent:
        bit = mask.bit_length() - 1
        hz_nom = next(hz for b, _, hz in BELLS if b == bit)
        frame = int(np.searchsorted(times, ts + offset))
        sel = np.flatnonzero((freqs > hz_nom * 0.95) & (freqs < hz_nom * 1.05))
        seg = spec[frame:frame + 20]
        idx = sel[np.argmax(seg[:, sel].max(axis=0))]
        # Parabolic interpolation around the peak bin for sub-bin accuracy.
        a, b, c = np.log(seg[:, idx - 1:idx + 2].max(axis=0) + 1e-12)
        shift = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0
        measured.setdefault(bit, []).append(freqs[idx] + shift * (freqs[1] - freqs[0]))

    centers = [float(np.median(measured[bit])) for bit, _, _ in BELLS]
    energy = band_energy(freqs, spec, centers)
    found = onsets(times, energy)
    rise = peak_rise(energy)
    templates, strengths = {}, {}
    for ts, mask in sent:
        bit = mask.bit_length() - 1
        expected = ts + offset
        near = [f for f in found if abs(times[f] - expected) < 0.1]
        frame = min(near, key=lambda f: abs(times[f] - expected)) if near \
            else int(np.searchsorted(times, expected))
        vec = strike_vector(energy, frame, rise)
        own = next(i for i, b in enumerate(BELLS) if b[0] == bit)
        if vec[own] > 0:
            templates.setdefault(bit, []).append(vec / vec[own])
            strengths.setdefault(bit, []).append(vec[own])

    print(f"recording latency {offset * 1000:.0f} ms")
    data = {"recording": str(wav), "latency_s": float(offset), "bells": {}}
    print(f"\n{'note':<5}{'nominal':>9}{'measured':>10}{'cents':>7}  bleed into other bells")
    for i, (bit, note, hz_nom) in enumerate(BELLS):
        if bit not in templates:
            raise RuntimeError(f"{note} (bit {bit}) was not heard during calibration")
        template = np.median(templates[bit], axis=0)
        cents = 1200 * np.log2(centers[i] / hz_nom)
        bleed = ", ".join(f"{BELLS[j][1]} {template[j]:.2f}" for j in np.argsort(-template)
                          if j != i and template[j] > 0.2)
        print(f"{note:<5}{hz_nom:9.1f}{centers[i]:10.1f}{cents:+7.0f}  {bleed or '-'}")
        data["bells"][str(bit)] = {"note": note, "hz": centers[i], "nominal_hz": hz_nom,
                                   "strength": float(np.median(strengths[bit])),
                                   "template": [float(v) for v in template]}
    CALIBRATION.write_text(json.dumps(data, indent=2))
    print(f"\nsaved {CALIBRATION}")
    return 0


# --- commands ------------------------------------------------------------------

def load_calibration():
    if not CALIBRATION.exists():
        sys.exit(f"no {CALIBRATION.name}; run 'bell_listen.py calibrate' first")
    return json.loads(CALIBRATION.read_text())


def run_performance(name, strikes, args, step_ms=None):
    calibration = load_calibration()
    step_ms = args.step_ms or step_ms or 200  # an explicit --step-ms wins
    print(f"playing {len(strikes)} frames at {step_ms:g} ms")
    wav, sent = perform(name, strikes, step_ms / 1000, args.port, args.device, args.pulse)
    return score(wav, sent, calibration)


def score(wav, sent, calibration):
    heard, times, energy = detect(read_wav(wav), calibration)
    offset = align(times, energy, sent)
    return 0 if report(sent, heard, offset, times, energy, wav) else 1


def cmd_rescore(args):
    """Re-analyze a saved performance, e.g. after recalibrating."""
    saved = json.loads(args.report.read_text())
    bits = {note: bit for bit, note, _ in BELLS}
    sent = [(row["sent_s"], sum(1 << bits[n] for n in row["sent"])) for row in saved["strikes"]]
    return score(Path(saved["recording"]), sent, load_calibration())


def cmd_scale(args):
    scale = rom_scale(ROM)
    return run_performance("scale", scale + scale[-2::-1], args)


def cmd_rom(args):
    text = args.header.read_text()
    words = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]+)", text)]
    words = words[args.start:args.start + args.frames]
    # Generated headers state their frame time; the original ROM's is 200 ms.
    stated = re.search(r"frame every ([0-9.]+) ms", text)
    return run_performance(args.header.stem, words, args, float(stated.group(1)) if stated else None)


def cmd_analyze(args):
    calibration = load_calibration()
    heard, _, _ = detect(read_wav(args.wav), calibration)
    for th, mask in heard:
        print(f"{th:7.2f}  {' '.join(mask_notes(mask))}")
    print(f"{len(heard)} strikes")
    return 0


def cmd_record(args):
    RECORDINGS.mkdir(parents=True, exist_ok=True)
    wav = args.output or RECORDINGS / f"{time.strftime('%Y%m%d-%H%M%S')}-take.wav"
    recorder = Recorder(Path(wav), args.device)
    print(f"recording {args.seconds:g} s to {wav}")
    try:
        time.sleep(args.seconds)
    finally:
        recorder.stop()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default="/dev/ttyACM1")
    parser.add_argument("--device", help="PipeWire/PulseAudio source (default: system default)")
    parser.add_argument("--step-ms", type=float,
                        help="frame interval when playing (default: the header's stated time, else 200)")
    parser.add_argument("--pulse", type=int, help="set the player's strike length (ms) before playing")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("calibrate", help="strike each bell alone and learn its pitch")
    p.add_argument("--repeats", type=int, default=2)
    p.set_defaults(func=calibrate)

    p = sub.add_parser("scale", help="play and check the ROM intro scale, down then up")
    p.set_defaults(func=cmd_scale)

    p = sub.add_parser("rom", help="play and check frames from a ROM header")
    p.add_argument("header", type=Path)
    p.add_argument("--start", type=int, default=0)
    p.add_argument("--frames", type=int, default=100000, help="default: the whole header")
    p.set_defaults(func=cmd_rom)

    p = sub.add_parser("rescore", help="re-analyze a saved performance from its JSON report")
    p.add_argument("report", type=Path)
    p.set_defaults(func=cmd_rescore)

    p = sub.add_parser("analyze", help="list the strikes heard in a WAV file")
    p.add_argument("wav", type=Path)
    p.set_defaults(func=cmd_analyze)

    p = sub.add_parser("record", help="just record the microphone")
    p.add_argument("seconds", type=float)
    p.add_argument("--output", type=Path)
    p.set_defaults(func=cmd_record)

    args = parser.parse_args()
    try:
        return args.func(args)
    except RuntimeError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
