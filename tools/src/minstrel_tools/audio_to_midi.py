#!/usr/bin/env python3
"""Transcribe a recording (MP3, WAV, ...) into a MIDI file for harmonize.py.

The output has three tracks: 0 holds the tempo, 1 the sung melody, and 2 one
chord per half bar. Feed it to harmonize.py with --melody-track 1
--harmony-track 2. The steps, and why each one is there, are in
docs/audio-to-midi.md:

1. Demucs splits the mix into drums, bass, other and vocals stems.
2. The drum stem's beats are tracked, and each beat is split into two
   eighth-note slots, so the grid follows the tempo as it drifts.
3. Basic Pitch transcribes the vocal stem polyphonically. Each grid slot keeps
   the highest note near the lead singer's range, so a harmony voice below
   the lead is skipped.
4. Each half bar gets the diatonic triad that best fits the bass and other
   stems.

Needs the venv in docs/audio-to-midi.md (demucs, librosa, basic-pitch on
onnxruntime). Stems and analysis are cached in --work, so later runs only
redo steps 3 and 4.

    minstrel-audio-to-midi SONG.mp3 \\
      --output media/audio/song.mid
"""

import argparse
import logging
import struct
import subprocess
import sys
from collections import Counter
from pathlib import Path

import numpy as np

from .paths import MEDIA

SR = 22050
STEMS = ("drums", "bass", "other", "vocals", "mix")
TPQ = 240
NAMES = "C C# D D# E F F# G G# A A# B".split()
# Krumhansl-Kessler major-key profile, for picking the key's diatonic triads.
MAJOR_PROFILE = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
TRIADS = [(0, "", (0, 4, 7)), (2, "m", (0, 3, 7)), (4, "m", (0, 3, 7)),
          (5, "", (0, 4, 7)), (7, "", (0, 4, 7)), (9, "m", (0, 3, 7))]


def separate(source, work):
    """Decode SOURCE and split it into stems in WORK, unless already done."""
    if all((work / f"{s}.wav").exists() for s in ("drums", "bass", "other", "vocals")):
        return
    import soundfile as sf
    import torch
    from demucs.apply import apply_model
    from demucs.pretrained import get_model

    mix = work / "mix.wav"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(source), "-ar", "44100", "-ac", "2", str(mix)],
                   check=True)
    wav, sr = sf.read(mix, dtype="float32", always_2d=True)
    device = "cuda" if torch.cuda.is_available() else "cpu"
    model = get_model("htdemucs").to(device).eval()
    x = torch.from_numpy(wav.T)[None].to(device)
    ref = x.mean(1)
    x = (x - ref.mean()) / ref.std()
    with torch.no_grad():
        y = apply_model(model, x, split=True, overlap=0.25, progress=False)[0]
    y = y * ref.std() + ref.mean()
    # torchaudio no longer saves WAVs without torchcodec, so write with soundfile.
    for name, stem in zip(model.sources, y):
        sf.write(work / f"{name}.wav", stem.cpu().numpy().T, sr)
    print(f"separated {len(wav) / sr:.1f} s into stems on {device}")


class Grid:
    """Eighth-note slots that follow the drummer's tracked beats.

    The tempo of a live recording drifts (399-409 ms beats over All the Small
    Things), so a constant grid ends up a whole eighth off by the end. Each
    tracked beat is split into two slots, and times before the first or after
    the last tracked beat continue at the median beat.
    """

    def __init__(self, beats):
        self.beats = beats
        self.period = float(np.median(np.diff(beats)))

    def _beat(self, t):
        b, n = self.beats, len(self.beats)
        if t < b[0]:
            return (t - b[0]) / self.period
        if t > b[-1]:
            return n - 1 + (t - b[-1]) / self.period
        return float(np.interp(t, b, np.arange(n)))

    def slot(self, t):
        return int(np.round(2 * self._beat(t)))

    def time(self, slot):
        b, n, beat = self.beats, len(self.beats), slot / 2
        if beat < 0:
            return b[0] + beat * self.period
        if beat > n - 1:
            return b[-1] + (beat - n + 1) * self.period
        return float(np.interp(beat, np.arange(n), b))


def track_beats(drums, hop=64):
    """Return beat times (s) from a stem (normally drums), with dropped beats filled in."""
    import librosa

    env = librosa.onset.onset_strength(y=drums, sr=SR, hop_length=hop)
    _, beats = librosa.beat.beat_track(onset_envelope=env, sr=SR, hop_length=hop, start_bpm=120,
                                       tightness=400, units="time")
    period = np.median(np.diff(beats))
    filled = [beats[0]]
    for t in beats[1:]:
        gap = round((t - filled[-1]) / period)
        filled += [filled[-1] + (t - filled[-1]) * i / gap for i in range(1, gap)] + [t]
    return np.array(filled)


def track_bars(bass, beats_per_bar, hop=512):
    """Return beat times (s) for music in BEATS_PER_BAR time, with a downbeat
    first and every BEATS_PER_BAR-th beat after it.

    In a waltz the drums play on every beat, so the beat tracker has no reason
    to keep a bar's beats in threes. Shostakovich's Waltz No. 2 locked onto
    one and a half beats and drifted between bars. The bass plays only on the
    downbeat, so bars are tracked on the bass stem and each is split evenly.
    """
    import librosa

    env = librosa.onset.onset_strength(y=bass, sr=SR, hop_length=hop)
    _, bars = librosa.beat.beat_track(onset_envelope=env, sr=SR, hop_length=hop, start_bpm=60,
                                      tightness=1000, units="time")
    beats = [a + (b - a) * i / beats_per_bar for a, b in zip(bars[:-1], bars[1:]) for i in range(beats_per_bar)]
    return np.array(beats + [bars[-1]])


def transcribe(stem_path):
    """Return Basic Pitch note events as an array of (start, end, midi, amplitude)."""
    logging.disable(logging.WARNING)
    from basic_pitch import ICASSP_2022_MODEL_PATH
    from basic_pitch.inference import predict

    _, _, events = predict(str(stem_path), ICASSP_2022_MODEL_PATH, onset_threshold=0.5,
                           frame_threshold=0.3, minimum_note_length=80, minimum_frequency=90,
                           maximum_frequency=900, melodia_trick=True)
    logging.disable(logging.NOTSET)
    return np.array([(start, end, pitch, amp) for start, end, pitch, amp, *_ in events])


def pick_melody(events, vocals, grid, span, gate, octave_window=None):
    """Choose one sung note per eighth slot: the highest one within SPAN
    semitones of the vocal stem's median pitch, loud enough to be singing.

    With OCTAVE_WINDOW (seconds), the center is the median of the sung notes
    within that many seconds instead, and each note moves by whole octaves
    from that local center to the song's, so a verse sung an octave below the
    chorus lands in one register.

    Returns a list of (slot, length in slots, midi note).
    """
    import librosa

    hop = 256
    rms = librosa.feature.rms(y=vocals, frame_length=2048, hop_length=hop)[0]
    times = librosa.times_like(rms, sr=SR, hop_length=hop)
    loud = np.percentile(rms, 95)
    center = np.median(events[:, 2])
    # Demucs leaves faint instrument bleed in the vocal stem during
    # instrumental parts, and Basic Pitch transcribes it at full strength.
    sung = []
    for start, end, pitch, amp in events:
        inside = (times >= start) & (times < end)
        if inside.any() and rms[inside].mean() >= gate * loud:
            sung.append((start, end, pitch, amp))
    sung = np.array(sung)
    bleed = len(events) - len(sung)
    candidates = {}
    shifted = 0
    for start, end, pitch, _ in sung:
        shift = 0
        local = center
        if octave_window:
            near = sung[np.abs(sung[:, 0] - start) <= octave_window, 2]
            local = np.median(near)
            shift = 12 * round((center - local) / 12)
        if abs(pitch - local) > span:
            continue
        pitch += shift
        shifted += shift != 0
        slot = grid.slot(start)
        length = max(1, grid.slot(end) - slot)
        if slot not in candidates or pitch > candidates[slot][1]:
            candidates[slot] = (length, int(pitch))
    notes = []
    for slot in sorted(candidates):
        length, pitch = candidates[slot]
        if notes and notes[-1][0] + notes[-1][1] > slot:
            notes[-1] = (notes[-1][0], slot - notes[-1][0], notes[-1][2])
        notes.append((slot, length, pitch))
    # Where the lead rests for one slot, the harmony voice below shows through
    # as a short dip. Fold those into the previous note.
    dips = [i for i in range(1, len(notes) - 1) if notes[i][1] == 1
            and notes[i - 1][2] - notes[i][2] >= 3 and notes[i + 1][2] - notes[i][2] >= 3]
    for i in reversed(dips):
        slot, length, pitch = notes[i - 1]
        notes[i - 1] = (slot, length + notes[i][1], pitch)
        del notes[i]
    print(f"melody: {len(notes)} notes from {len(events)} events "
          f"({bleed} bleed, {len(dips)} harmony dips dropped"
          + (f", {shifted} moved an octave" if octave_window else "") + ")")
    return notes


def pick_intro(events, vocal_events, grid, span, before):
    """Choose the top note per eighth slot of an instrument stem, for the slots
    before BEFORE, so an instrumental intro gets its hook as the melody. Only
    notes within SPAN semitones of the vocals' median pitch count, so the
    intro sits in the singer's register and takes the same --transpose.

    Returns a list of (slot, 1, midi note).
    """
    center = np.median(vocal_events[:, 2])
    top = {}
    for start, _, pitch, _ in events:
        slot = grid.slot(start)
        if slot < before and abs(pitch - center) <= span and pitch > top.get(slot, 0):
            top[slot] = int(pitch)
    print(f"intro: {len(top)} notes from the other stem before {grid.time(before):.1f} s")
    return [(slot, 1, top[slot]) for slot in sorted(top)]


def pick_chords(bass, other, grid, slots, key=None, window=4):
    """Return (key name, list of (slot, WINDOW, chord name, pitch classes)), one
    chord per WINDOW slots: 4 is half a bar of 4/4.

    KEY is the major key's tonic as a pitch class; None detects it.
    """
    import librosa

    tuning = librosa.estimate_tuning(y=other, sr=SR)
    chroma = librosa.feature.chroma_cqt(y=bass + other, sr=SR, hop_length=512, tuning=tuning)
    bass_chroma = librosa.feature.chroma_cqt(y=bass, sr=SR, hop_length=512, tuning=tuning)
    times = librosa.times_like(chroma, sr=SR, hop_length=512)
    total = chroma.mean(1)
    if key is None:
        key = max(range(12), key=lambda k: np.corrcoef(np.roll(MAJOR_PROFILE, k), total)[0, 1])
    triads = {f"{NAMES[(key + degree) % 12]}{quality}": tuple((key + degree + i) % 12 for i in shape)
              for degree, quality, shape in TRIADS}
    chords = []
    for slot in range(0, slots, window):
        inside = (times >= grid.time(slot)) & (times < grid.time(slot + window))
        if not inside.any():
            continue
        c, b = chroma[:, inside].mean(1), bass_chroma[:, inside].mean(1)
        if c.max() < 0.2:
            continue
        # Root in the bass counts extra, which also settles power chords.
        name = max(triads, key=lambda n: c[list(triads[n])].sum() + 1.5 * b[triads[n][0]])
        chords.append((slot, window, name, triads[name]))
    return f"{NAMES[key]} major", chords


def vlq(n):
    if n < 0:
        raise ValueError(f"negative MIDI delta {n}")
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def track_chunk(events, name):
    """EVENTS are (tick, order, message); order 0 (note off) sorts before 1 (note on)."""
    data, last = bytes([0, 0xFF, 0x03, len(name)]) + name.encode(), 0
    for tick, _, message in sorted(events, key=lambda e: (e[0], e[1])):
        data += vlq(tick - last) + message
        last = tick
    data += b"\x00\xFF\x2F\x00"
    return b"MTrk" + struct.pack(">I", len(data)) + data


def write_midi(path, period, melody, chords, bar_slots=1):
    ticks = TPQ // 2
    # Notes before the first tracked beat can land on negative slots; MIDI
    # ticks can't be negative, so shift everything to start at slot 0. Shift
    # by whole bars, so harmonize.py's --chord-every lands on downbeats.
    first = min([slot for slot, *_ in melody] + [slot for slot, *_ in chords] + [0]) // bar_slots * bar_slots
    melody = [(slot - first, *rest) for slot, *rest in melody]
    chords = [(slot - first, *rest) for slot, *rest in chords]
    tempo = track_chunk([(0, 0, b"\xFF\x51\x03" + int(round(period * 1e6)).to_bytes(3, "big"))], "tempo")
    mel = []
    for slot, length, pitch in melody:
        mel += [(slot * ticks, 1, bytes([0x90, pitch, 100])), ((slot + length) * ticks, 0, bytes([0x80, pitch, 0]))]
    har = []
    for slot, length, _, pcs in chords:
        for pc in pcs:
            har += [(slot * ticks, 1, bytes([0x91, 48 + pc, 80])), ((slot + length) * ticks, 0, bytes([0x81, 48 + pc, 0]))]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"MThd" + struct.pack(">IHHH", 6, 1, 3, TPQ) + tempo
                     + track_chunk(mel, "Vocals") + track_chunk(har, "Chords"))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("audio", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="MIDI file to write")
    parser.add_argument("--work", type=Path, help="cache for stems and analysis (default: media/audio/<name>/)")
    parser.add_argument("--span", type=float, default=7, help="melody semitones either side of the median pitch")
    parser.add_argument("--gate", type=float, default=0.3,
                        help="minimum vocal-stem level for a note, as a fraction of its 95th-percentile level")
    parser.add_argument("--octave-window", type=float,
                        help="center the span on the median of the sung notes within this many seconds, "
                             "and move each note by octaves to the song's register, for a singer who "
                             "changes octave between sections, e.g. 4")
    parser.add_argument("--intro", action="store_true",
                        help="before the vocals start, use the other stem's top line as the melody")
    parser.add_argument("--key", choices=NAMES, help="major key for the chords, e.g. C# (default: detect)")
    parser.add_argument("--beat-stem", choices=STEMS, default="drums",
                        help="stem to track beats on; mix or other for music without drums")
    parser.add_argument("--melody-stem", choices=STEMS, default="vocals",
                        help="stem to take the melody from; other for instrumental music")
    parser.add_argument("--melody-from", type=float, default=0,
                        help="drop melody notes before this time (s), e.g. stray accompaniment notes "
                             "before an instrumental melody enters")
    parser.add_argument("--beats-per-bar", type=int,
                        help="track bars on the bass stem and give each bar this many beats and one "
                             "chord, e.g. 3 for a waltz (default: track beats, a chord per half bar)")
    args = parser.parse_args()

    import librosa

    work = args.work or MEDIA / "audio" / args.audio.stem
    work.mkdir(parents=True, exist_ok=True)
    separate(args.audio, work)
    load = {s: librosa.load(work / f"{s}.wav", sr=SR)[0] for s in STEMS}

    if args.beats_per_bar:
        beats = work / f"beats_bars{args.beats_per_bar}.npy"
        if not beats.exists():
            np.save(beats, track_bars(load["bass"], args.beats_per_bar))
    else:
        beats = work / ("beats.npy" if args.beat_stem == "drums" else f"beats_{args.beat_stem}.npy")
    if not beats.exists():
        # librosa's beat tracker needs over 20 GB on a full-band stem's onset
        # envelope at hop 64 (181k frames for 8.8 min); hop 512 peaks under 1 GB.
        np.save(beats, track_beats(load[args.beat_stem], hop=64 if args.beat_stem == "drums" else 512))
    grid = Grid(np.load(beats))
    local = [np.median(np.diff(grid.beats[i:i + 64])) for i in range(0, len(grid.beats) - 8, 64)]
    print(f"tempo {60 / grid.period:.2f} BPM ({grid.period * 500:.0f} ms eighths), "
          f"{len(grid.beats)} beats {grid.beats[0]:.2f}-{grid.beats[-1]:.2f} s, "
          f"local beat {min(local) * 1000:.0f}-{max(local) * 1000:.0f} ms")

    notes = work / ("vocal_notes.npy" if args.melody_stem == "vocals" else f"{args.melody_stem}_notes.npy")
    if not notes.exists():
        np.save(notes, transcribe(work / f"{args.melody_stem}.wav"))
    events = np.load(notes)

    melody = pick_melody(events, load[args.melody_stem], grid, args.span, args.gate, args.octave_window)
    melody = [note for note in melody if grid.time(note[0]) >= args.melody_from]
    if args.intro:
        other_notes = work / "other_notes.npy"
        if not other_notes.exists():
            np.save(other_notes, transcribe(work / "other.wav"))
        melody = pick_intro(np.load(other_notes), events, grid, args.span, melody[0][0]) + melody
    slots = grid.slot(len(load["drums"]) / SR) + 1
    window = 2 * args.beats_per_bar if args.beats_per_bar else 4
    key, chords = pick_chords(load["bass"], load["other"], grid, slots,
                              NAMES.index(args.key) if args.key else None, window)
    print(f"key {key}; chords: {dict(Counter(name for _, _, name, _ in chords))}")
    print(f"melody range {NAMES[min(n[2] for n in melody) % 12]}{min(n[2] for n in melody) // 12 - 1}"
          f"-{NAMES[max(n[2] for n in melody) % 12]}{max(n[2] for n in melody) // 12 - 1}, "
          f"first note at {grid.time(melody[0][0]):.1f} s")
    write_midi(args.output, grid.period, melody, chords, window if args.beats_per_bar else 1)
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
