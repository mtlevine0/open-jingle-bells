#!/usr/bin/env python3
"""Convert a Standard MIDI track into a 12-bit bell ROM and WAV preview."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import struct
import sys

from . import rom_player


# Bell positions 1-12 from firmware/bell_pintout2.xlsx. Values are MIDI notes.
BELL_NOTES = (
    (0x008, 100, "E7"),
    (0x080, 98, "D7"),
    (0x040, 96, "C7"),
    (0x020, 95, "B6"),
    (0x010, 94, "A#6"),
    (0x100, 93, "A6"),
    (0x200, 91, "G6"),
    (0x400, 90, "F#6"),
    (0x800, 89, "F6"),
    (0x001, 88, "E6"),
    (0x002, 86, "D6"),
    (0x004, 84, "C6"),
)
MASK_BY_MIDI = {midi_note: mask for mask, midi_note, _ in BELL_NOTES}
AVAILABLE_MIDI = tuple(sorted(MASK_BY_MIDI))


@dataclass(frozen=True)
class MidiNote:
    tick: int
    note: int
    velocity: int
    channel: int


@dataclass(frozen=True)
class MidiTrack:
    number: int
    name: str
    notes: tuple[MidiNote, ...]


def read_vlq(data: bytes, position: int) -> tuple[int, int]:
    value = 0
    while True:
        if position >= len(data):
            raise ValueError("Truncated MIDI variable-length value")
        byte = data[position]
        position += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, position


def parse_track(number: int, data: bytes) -> tuple[MidiTrack, list[tuple[int, int]]]:
    position = 0
    tick = 0
    running_status: int | None = None
    name = ""
    notes: list[MidiNote] = []
    tempos: list[tuple[int, int]] = []

    while position < len(data):
        delta, position = read_vlq(data, position)
        tick += delta
        status = data[position]
        if status < 0x80:
            if running_status is None:
                raise ValueError(f"Track {number}: running status without prior status")
            status = running_status
        else:
            position += 1
            if status < 0xF0:
                running_status = status

        if status == 0xFF:
            meta_type = data[position]
            position += 1
            length, position = read_vlq(data, position)
            payload = data[position : position + length]
            position += length
            if meta_type == 0x03:
                name = payload.decode("utf-8", errors="replace")
            elif meta_type == 0x51 and length == 3:
                tempos.append((tick, int.from_bytes(payload, "big")))
            continue

        if status in (0xF0, 0xF7):
            length, position = read_vlq(data, position)
            position += length
            continue

        event_type = status & 0xF0
        channel = status & 0x0F
        length = 1 if event_type in (0xC0, 0xD0) else 2
        payload = data[position : position + length]
        if len(payload) != length:
            raise ValueError(f"Track {number}: truncated channel event")
        position += length
        if event_type == 0x90 and payload[1] > 0:
            notes.append(MidiNote(tick, payload[0], payload[1], channel))

    return MidiTrack(number, name, tuple(notes)), tempos


def parse_midi(path: Path) -> tuple[int, list[MidiTrack], list[tuple[int, int]]]:
    data = path.read_bytes()
    if data[:4] != b"MThd" or len(data) < 14:
        raise ValueError(f"{path} is not a Standard MIDI file")
    header_length = struct.unpack(">I", data[4:8])[0]
    midi_format, track_count, division = struct.unpack(">HHH", data[8:14])
    if midi_format not in (0, 1):
        raise ValueError(f"Unsupported MIDI format {midi_format}")
    if division & 0x8000:
        raise ValueError("SMPTE time division is not supported")

    position = 8 + header_length
    tracks: list[MidiTrack] = []
    tempos: list[tuple[int, int]] = []
    for number in range(track_count):
        if data[position : position + 4] != b"MTrk":
            raise ValueError(f"Missing MIDI track chunk {number}")
        length = struct.unpack(">I", data[position + 4 : position + 8])[0]
        start = position + 8
        track, track_tempos = parse_track(number, data[start : start + length])
        tracks.append(track)
        tempos.extend(track_tempos)
        position = start + length
    return division, tracks, sorted(tempos)


def tick_to_seconds(tick: int, ticks_per_quarter: int, tempos: list[tuple[int, int]]) -> float:
    tempo_events = tempos or [(0, 500_000)]
    if tempo_events[0][0] != 0:
        tempo_events = [(0, 500_000), *tempo_events]
    seconds = 0.0
    prior_tick = 0
    tempo = tempo_events[0][1]
    for event_tick, next_tempo in tempo_events[1:]:
        if event_tick >= tick:
            break
        seconds += (event_tick - prior_tick) * tempo / ticks_per_quarter / 1_000_000
        prior_tick = event_tick
        tempo = next_tempo
    seconds += (tick - prior_tick) * tempo / ticks_per_quarter / 1_000_000
    return seconds


def map_note(note: int) -> tuple[int, int, str]:
    """Octave-fold a MIDI note into the bell range, then use the nearest bell."""
    folded = note
    while folded < AVAILABLE_MIDI[0]:
        folded += 12
    while folded > AVAILABLE_MIDI[-1]:
        folded -= 12
    mapped = min(AVAILABLE_MIDI, key=lambda candidate: (abs(candidate - folded), candidate))
    mask = MASK_BY_MIDI[mapped]
    name = next(name for candidate_mask, candidate, name in BELL_NOTES if candidate_mask == mask and candidate == mapped)
    return mask, mapped, name


def convert_track(
    track: MidiTrack,
    ticks_per_quarter: int,
    tempos: list[tuple[int, int]],
    step_ms: float,
    trailing_rest_frames: int,
    voice: str,
    frames_per_quarter: float | None,
    transpose: int = 0,
) -> tuple[list[int], dict[tuple[int, int], int], int, int]:
    if not track.notes:
        raise ValueError(f"Track {track.number} has no note-on events")

    grouped: dict[int, list[MidiNote]] = {}
    for note in track.notes:
        grouped.setdefault(note.tick, []).append(note)
    if voice == "lowest":
        selected = [min(notes, key=lambda note: note.note) for _, notes in sorted(grouped.items())]
    elif voice == "highest":
        selected = [max(notes, key=lambda note: note.note) for _, notes in sorted(grouped.items())]
    else:
        selected = list(track.notes)

    first_tick = min(note.tick for note in selected)
    first_time = min(tick_to_seconds(note.tick, ticks_per_quarter, tempos) for note in selected)
    frames: dict[int, int] = {}
    substitutions: dict[tuple[int, int], int] = {}
    timing_collisions = 0
    last_tick: int | None = None
    last_frame = -1
    for event in selected:
        if frames_per_quarter is None:
            time_s = tick_to_seconds(event.tick, ticks_per_quarter, tempos) - first_time
            frame = max(0, round(time_s * 1000.0 / step_ms))
        else:
            frame = max(0, round((event.tick - first_tick) * frames_per_quarter / ticks_per_quarter))
        if voice != "all" and last_tick is not None and event.tick > last_tick and frame <= last_frame:
            frame = last_frame + 1
            timing_collisions += 1
        mask, mapped_note, _ = map_note(event.note + transpose)
        frames[frame] = frames.get(frame, 0) | mask
        if mapped_note % 12 != (event.note + transpose) % 12:
            key = ((event.note + transpose) % 12, mapped_note % 12)
            substitutions[key] = substitutions.get(key, 0) + 1
        last_tick = event.tick
        last_frame = frame
    output = [0] * (max(frames) + 1 + trailing_rest_frames)
    for frame, word in frames.items():
        output[frame] = word
    return output, substitutions, len(selected), timing_collisions


def avoid_repeats(words: list[int], min_gap: int) -> tuple[list[int], int, int]:
    """Re-voice strikes that repeat a bell within min_gap frames.

    A bell struck again while it is still ringing barely sounds and can choke
    itself (measured with minstrel-bell-listen). Such a repeat moves
    to the same pitch an octave away, else to a third below or above that is in
    the song's key, as long as that bell is also free; otherwise it is dropped
    and the first strike keeps ringing. Returns (words, moved, dropped).
    """
    midi_by_mask = {mask: midi for mask, midi, _ in BELL_NOTES}
    mask_by_midi = {midi: mask for mask, midi, _ in BELL_NOTES}
    in_key = {midi_by_mask[1 << bit] % 12 for word in words for bit in range(12) if word >> bit & 1}
    out = list(words)
    moved = dropped = 0

    def busy(mask: int, frame: int) -> bool:
        window = out[max(0, frame - min_gap):frame] + out[frame + 1:frame + 1 + min_gap]
        return any(word & mask for word in window)

    for frame, word in enumerate(out):
        for bit in range(12):
            mask = 1 << bit
            if not word & mask or not any(w & mask for w in out[max(0, frame - min_gap):frame]):
                continue
            midi = midi_by_mask[mask]
            octave = [m for m in (midi - 12, midi + 12) if m in mask_by_midi]
            thirds = [m for m in (midi - 3, midi - 4, midi + 3, midi + 4)
                      if m in mask_by_midi and m % 12 in in_key]
            choice = next((m for m in octave + thirds
                           if not out[frame] & mask_by_midi[m] and not busy(mask_by_midi[m], frame)), None)
            out[frame] &= ~mask
            if choice is None:
                dropped += 1
            else:
                out[frame] |= mask_by_midi[choice]
                moved += 1
            word = out[frame]
    return out, moved, dropped


def write_header(path: Path, words: list[int], source: Path, track: MidiTrack, step_ms: float) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"// Generated from {source.name}, track {track.number} ({track.name or 'unnamed'})",
        f"// One 12-bit frame every {step_ms:g} ms; {len(words)} frames total.",
        *(f"0x{word:03X}," for word in words),
        "",
    ]
    path.write_text("\n".join(lines))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("midi", type=Path)
    parser.add_argument("--list-tracks", action="store_true")
    parser.add_argument("--track", type=int, default=1, help="zero-based MIDI track number")
    parser.add_argument("--step-ms", type=float, default=rom_player.DEFAULT_STEP_MS)
    parser.add_argument("--voice", choices=("all", "lowest", "highest"), default="all")
    parser.add_argument(
        "--frames-per-quarter",
        type=float,
        help="quantize by musical time instead of MIDI tempo (3 preserves this file's triplet grid)",
    )
    parser.add_argument("--trailing-rest-frames", type=int, default=15)
    parser.add_argument(
        "--transpose",
        type=int,
        default=0,
        help="shift the whole track by this many semitones before fitting notes to the bells; "
        "a shift that lands the melody inside C6-E7 keeps its shape (notes are otherwise folded "
        "into range one octave at a time, which can flatten leaps)",
    )
    parser.add_argument(
        "--avoid-repeats",
        type=int,
        default=0,
        metavar="FRAMES",
        help="re-voice a bell repeated within this many frames (2 suits 200 ms frames)",
    )
    parser.add_argument("--output-header", type=Path)
    parser.add_argument("--output-wav", type=Path)
    parser.add_argument("--sample-rate", type=int, default=44100)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        ticks_per_quarter, tracks, tempos = parse_midi(args.midi)
        if args.list_tracks:
            for track in tracks:
                note_range = "-"
                if track.notes:
                    note_range = f"{min(note.note for note in track.notes)}..{max(note.note for note in track.notes)}"
                print(f"{track.number:2d}  {track.name or '(unnamed)':16s} notes={len(track.notes):4d} range={note_range}")
            return 0
        if not 0 <= args.track < len(tracks):
            raise ValueError(f"--track must be between 0 and {len(tracks) - 1}")
        if args.step_ms <= 0:
            raise ValueError("--step-ms must be greater than zero")
        if not args.output_header and not args.output_wav:
            raise ValueError("Specify --output-header, --output-wav, or --list-tracks")

        track = tracks[args.track]
        words, substitutions, selected_count, timing_collisions = convert_track(
            track,
            ticks_per_quarter,
            tempos,
            args.step_ms,
            args.trailing_rest_frames,
            args.voice,
            args.frames_per_quarter,
            args.transpose,
        )
        if args.avoid_repeats:
            words, moved, dropped = avoid_repeats(words, args.avoid_repeats)
            print(f"Re-voiced {moved} and dropped {dropped} same-bell repeats within {args.avoid_repeats} frames")
        if args.output_header:
            write_header(args.output_header, words, args.midi, track, args.step_ms)
            print(f"Wrote {len(words)} ROM frames to {args.output_header}")
        if args.output_wav:
            rom_player.render_wav(words, args.output_wav, args.step_ms, args.sample_rate, 0.22)
            print(f"Rendered WAV preview to {args.output_wav}")
        duration = len(words) * args.step_ms / 1000.0
        print(
            f"Track {track.number} ({track.name or 'unnamed'}), {args.voice} voice: "
            f"selected {selected_count} of {len(track.notes)} note-ons, {duration:.1f} seconds"
        )
        if substitutions:
            count = sum(substitutions.values())
            print(f"Snapped {count} unavailable chromatic notes to the nearest bell")
        if timing_collisions:
            print(f"Moved {timing_collisions} quantized melody events forward to preserve their order")
        return 0
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
