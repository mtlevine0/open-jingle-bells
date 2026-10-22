#!/usr/bin/env python3
"""Analyze and audibly preview a Ye Merrie Minstrel ROM dump.

The original ROM plays one 12-bit word every 200 ms. Each set bit strikes one
bell; zero is a rest. A unit holds selected output lines high for 40 ms.
This tool mirrors the 200 ms event grid and renders each strike as a decaying
bell-like tone using only the Python standard library.
"""

from __future__ import annotations

import argparse
from array import array
from dataclasses import dataclass
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import wave

from .paths import ROM_DUMP


DEFAULT_ROM = ROM_DUMP
DEFAULT_STEP_MS = 200.0
HARDWARE_PULSE_MS = 40.0

# The ROM's startup diagnostic strikes the bells in this order. The pitches
# match the physical 12-bell set, descending from E5 to C4.
PITCHES = (
    (0x008, "E5", 659.26),
    (0x080, "D5", 587.32),
    (0x040, "C5", 523.26),
    (0x020, "B4", 493.88),
    (0x010, "Bb4", 466.16),
    (0x100, "A4", 440.00),
    (0x200, "G4", 392.00),
    (0x400, "F#4", 369.99),
    (0x800, "F4", 349.23),
    (0x001, "E4", 329.63),
    (0x002, "D4", 293.66),
    (0x004, "C4", 261.63),
)
PITCH_BY_MASK = {mask: (name, frequency) for mask, name, frequency in PITCHES}


@dataclass(frozen=True)
class Song:
    number: int
    title: str
    start_frame: int
    end_frame: int

    @property
    def frame_count(self) -> int:
        return self.end_frame - self.start_frame


# Regions were identified from the long inter-song rests and checked against
# the printed module-801 program order. End frames are exclusive.
SONGS_801 = (
    Song(1, "We Wish You A Merry Christmas", 109, 635),
    Song(2, "God Rest Ye Merry Gentlemen", 650, 964),
    Song(3, "Joy to the World", 977, 1290),
    Song(4, "Angels We Have Heard on High", 1311, 1808),
    Song(5, "Deck the Halls", 1821, 1954),
    Song(6, "Silent Night", 1965, 2098),
    Song(7, "Jingle Bells", 2111, 2360),
    Song(8, "Here We Come a Caroling", 2380, 2650),
    Song(9, "The First Noel", 2669, 2954),
    Song(10, "Hark the Herald Angels Sing", 2967, 3452),
    Song(11, "Away in a Manger", 3465, 3750),
    Song(12, "O Come All Ye Faithful", 3763, 4144),
    Song(13, "The Holly and the Ivy", 4157, 4298),
    Song(14, "We Three Kings of Orient Are", 4311, 4516),
    Song(15, "Bring A Torch", 4534, 4855),
    Song(16, "While Shepherds Watched their Flocks", 4868, 5058),
    Song(17, "Frosty the Snowman", 5078, 5327),
    Song(18, "Angels from the Realm of Glory", 5347, 5536),
    Song(19, "It Came Upon a Midnight Clear", 5553, 5747),
    Song(20, "O Little Town of Bethlehem", 5760, 6012),
    Song(21, "O Holy Night", 6026, 6639),
    Song(22, "Twelve days of Christmas", 6653, 7530),
    Song(23, "Have Yourself a Merry Little Christmas", 7550, 7831),
    Song(24, "Come All Ye Shepherds", 7851, 8098),
    Song(25, "Parade of the Tin Soldiers", 8111, 8330),
)


def parse_rom(path: Path) -> list[int]:
    """Parse comma-separated C hex literals from rom_dump_hex.h."""
    words = [int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", path.read_text())]
    if not words:
        raise ValueError(f"No hexadecimal ROM words found in {path}")
    invalid = [(index, word) for index, word in enumerate(words) if word > 0xFFF]
    if invalid:
        index, word = invalid[0]
        raise ValueError(f"Frame {index} contains non-12-bit value 0x{word:X}")
    return words


def song_by_number(number: int) -> Song:
    if not 1 <= number <= len(SONGS_801):
        raise ValueError(f"Song must be between 1 and {len(SONGS_801)}")
    try:
        return SONGS_801[number - 1]
    except IndexError as error:
        raise ValueError(f"Song must be between 1 and {len(SONGS_801)}") from error


def note_names(word: int) -> list[str]:
    return [name for mask, name, _ in PITCHES if word & mask]


def print_analysis(words: list[int], step_ms: float) -> None:
    nonzero = sum(word != 0 for word in words)
    chords = sum(word.bit_count() > 1 for word in words)
    duration = len(words) * step_ms / 1000.0
    print(f"ROM frames: {len(words)}")
    print(f"Playback grid: {step_ms:g} ms")
    print(f"Receiver strike pulse: {HARDWARE_PULSE_MS:g} ms")
    print(f"Loop duration: {duration:.1f} s ({duration / 60:.2f} min)")
    print(f"Strike frames: {nonzero}; chord frames: {chords}; rest frames: {len(words) - nonzero}")
    print("Module: 801 (Christmas Favorites)")
    print("Startup diagnostic: frames 0-108")
    print()
    print(" #  start     end   duration  title")
    for song in SONGS_801:
        start = song.start_frame * step_ms / 1000.0
        end = song.end_frame * step_ms / 1000.0
        song_duration = song.frame_count * step_ms / 1000.0
        print(f"{song.number:2d}  {start:7.1f}s {end:7.1f}s {song_duration:7.1f}s  {song.title}")


def selection(words: list[int], song_number: int | None, start: int | None, end: int | None) -> tuple[list[int], str]:
    if song_number is not None and (start is not None or end is not None):
        raise ValueError("Use --song or a custom frame range, not both")
    if song_number is not None:
        song = song_by_number(song_number)
        return words[song.start_frame : song.end_frame], song.title
    first = 0 if start is None else start
    last = len(words) if end is None else end
    if first < 0 or last > len(words) or first >= last:
        raise ValueError(f"Invalid frame range {first}:{last} for {len(words)} frames")
    return words[first:last], f"frames {first}:{last}"


def render_wav(words: list[int], output: Path, step_ms: float, sample_rate: int, gain: float) -> None:
    """Render ROM words as a simple struck-bell WAV file."""
    step_samples = round(sample_rate * step_ms / 1000.0)
    tail_samples = round(sample_rate * 1.8)
    sample_count = len(words) * step_samples + tail_samples
    mix = array("f", [0.0]) * sample_count

    # A slightly inharmonic, rapidly attacked, slowly decaying bell timbre.
    partials = ((1.0, 1.0, 1.45), (2.01, 0.36, 2.4), (3.93, 0.16, 3.5))
    strike_samples = round(sample_rate * 1.8)
    attack_samples = max(1, round(sample_rate * 0.004))

    for frame_index, word in enumerate(words):
        if word == 0:
            continue
        onset = frame_index * step_samples
        active = [(name, frequency) for mask, (name, frequency) in PITCH_BY_MASK.items() if word & mask]
        chord_gain = gain / math.sqrt(len(active))
        for _, frequency in active:
            for offset in range(strike_samples):
                target = onset + offset
                if target >= sample_count:
                    break
                time_s = offset / sample_rate
                attack = min(1.0, offset / attack_samples)
                value = 0.0
                for ratio, amplitude, decay in partials:
                    value += amplitude * math.sin(2.0 * math.pi * frequency * ratio * time_s) * math.exp(-decay * time_s)
                mix[target] += chord_gain * attack * value

    peak = max((abs(value) for value in mix), default=1.0)
    scale = 0.96 / peak if peak > 0.96 else 1.0
    pcm = array("h", (round(max(-1.0, min(1.0, value * scale)) * 32767) for value in mix))
    output.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm.tobytes())


def play_file(path: Path) -> None:
    # Prefer desktop audio backends on Linux. `aplay` may be installed even
    # when ALSA has no usable default PCM (common on PipeWire/PulseAudio
    # desktops), so a failed player must not prevent trying the next one.
    players = (
        ("pw-play", ["pw-play"]),
        ("paplay", ["paplay"]),
        ("ffplay", ["ffplay", "-nodisp", "-autoexit", "-loglevel", "error"]),
        ("aplay", ["aplay"]),
        ("afplay", ["afplay"]),
    )
    failures = []
    for executable, command in players:
        if shutil.which(executable):
            result = subprocess.run([*command, str(path)], check=False)
            if result.returncode == 0:
                return
            failures.append(f"{executable} exited with status {result.returncode}")
    if failures:
        raise RuntimeError("Audio playback failed: " + "; ".join(failures))
    raise RuntimeError("No supported audio player found (tried pw-play, paplay, ffplay, aplay, and afplay)")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM, help="ROM header to read")
    parser.add_argument("--step-ms", type=float, default=DEFAULT_STEP_MS, help="milliseconds per ROM frame")
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("analyze", help="show ROM statistics and identified module-801 songs")

    for command in ("render", "play"):
        sub = subparsers.add_parser(command, help=f"{command} the whole dump or a selected song")
        sub.add_argument("--song", type=int, help="module-801 song number, 1-25")
        sub.add_argument("--start-frame", type=int, help="first frame for a custom selection")
        sub.add_argument("--end-frame", type=int, help="exclusive last frame for a custom selection")
        sub.add_argument("--sample-rate", type=int, default=44100)
        sub.add_argument("--gain", type=float, default=0.22)
        if command == "render":
            sub.add_argument("--output", type=Path, required=True, help="destination WAV file")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if args.step_ms <= 0:
            raise ValueError("--step-ms must be greater than zero")
        words = parse_rom(args.rom)
        if args.command == "analyze":
            print_analysis(words, args.step_ms)
            return 0

        if args.sample_rate <= 0:
            raise ValueError("--sample-rate must be greater than zero")
        if args.gain <= 0:
            raise ValueError("--gain must be greater than zero")
        chosen, label = selection(words, args.song, args.start_frame, args.end_frame)
        if args.command == "render":
            output = args.output
            render_wav(chosen, output, args.step_ms, args.sample_rate, args.gain)
            print(f"Rendered {label} to {output}")
            return 0

        with tempfile.TemporaryDirectory(prefix="merrie-minstrel-") as directory:
            output = Path(directory) / "preview.wav"
            render_wav(chosen, output, args.step_ms, args.sample_rate, args.gain)
            print(f"Playing {label}")
            play_file(output)
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
