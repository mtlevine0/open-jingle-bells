#!/usr/bin/env python3
"""Arrange a MIDI melody with chords for the bells.

The melody comes from one MIDI track and the harmony from another (usually a
piano track's chords). At most --max-bells bells are struck per frame, and no
bell is struck within --min-gap frames of its last strike, because a bell
struck again while it is still ringing barely sounds (see docs/composing.md).

When the melody repeats a bell too soon, the bell is left ringing and chord
tones under it supply the new attack, instead of moving the note to another
octave. Chords are also added on every --chord-every quarter notes.

Frames don't have to be 200 ms: by default the frame time follows the MIDI
tempo (one frame per eighth note with --frames-per-quarter 2). --intro plays
the harmony track's top voice as the melody until the melody track starts.
--strum-intro instead strikes the harmony's chords in a rhythm pattern there,
for an intro whose hook is a strummed riff rather than a tune.

    ojb-harmonize ~/Downloads/FROSTY.MID \\
      --melody-track 2 --harmony-track 3 --transpose 24 --intro \\
      --output-header tools/songs/frosty_chords_rom.h
"""

import argparse
import math
import sys
from pathlib import Path

from . import midi_to_bells as mtb
from . import rom_player

MIDI_BY_MASK = {mask: midi for mask, midi, _ in mtb.BELL_NOTES}
MASK_BY_MIDI = {midi: mask for mask, midi, _ in mtb.BELL_NOTES}
NAMES = "C C# D D# E F F# G G# A A# B".split()


def name(midi):
    return f"{NAMES[midi % 12]}{midi // 12 - 1}"


def chords_by_tick(track, max_age_ticks, transpose=0):
    """Return chord_at(tick) -> set of pitch classes sounding in the harmony track,
    shifted by the same transposition as the melody."""
    groups = {}
    for note in track.notes:
        groups.setdefault(note.tick, set()).add((note.note + transpose) % 12)
    ticks = sorted(groups)

    def chord_at(tick):
        # The latest harmony onset at or before tick, if it's recent enough.
        lo, hi = 0, len(ticks)
        while lo < hi:
            mid = (lo + hi) // 2
            if ticks[mid] <= tick:
                lo = mid + 1
            else:
                hi = mid
        if lo == 0 or tick - ticks[lo - 1] > max_age_ticks:
            return set()
        return groups[ticks[lo - 1]]

    return chord_at


def bells(word):
    return [MIDI_BY_MASK[1 << bit] for bit in range(12) if word >> bit & 1]


def arrange(melody, chord_at, frame_tick, ticks_per_quarter, chord_every, max_bells, min_gap):
    out = list(melody)
    stats = {"masked": 0, "unmasked": 0, "chords": 0}

    def free(mask, frame):
        window = out[max(0, frame - min_gap):frame] + out[frame + 1:frame + 1 + min_gap]
        return not any(word & mask for word in window)

    def add_harmony(frame, top, wanted):
        """Add up to `wanted` chord tones below `top` on free bells."""
        chord = chord_at(frame_tick(frame))
        nearby = out[max(0, frame - min_gap):frame] + out[frame + 1:frame + 1 + min_gap]
        recent = {midi for word in nearby for midi in bells(word)}
        added = 0
        floor = top
        for midi in sorted(MASK_BY_MIDI, reverse=True):
            if added == wanted or len(bells(out[frame])) >= max_bells:
                break
            mask = MASK_BY_MIDI[midi]
            # Chord tones only, a third or more under the previous voice, and never
            # an octave from the melody or a bell rung just before or after (that
            # would sound like the melody leaping an octave).
            if (midi % 12 in chord and midi % 12 != top % 12 and midi <= floor - 3
                    and midi - 12 not in recent and midi + 12 not in recent
                    and not out[frame] & mask and free(mask, frame)):
                out[frame] |= mask
                floor = midi
                added += 1
        return added

    # Melody repeats first: drop the re-strike and let a chord supply the attack.
    ringing = {}
    for frame, word in enumerate(melody):
        if not word:
            continue
        top = max(bells(word))
        mask = MASK_BY_MIDI[top]
        last = ringing.get(mask)
        if last is not None and frame - last <= min_gap:
            out[frame] &= ~mask
            if add_harmony(frame, top, max_bells - 1):
                stats["masked"] += 1
            else:
                stats["unmasked"] += 1  # the bell is still ringing the note
            continue
        ringing[mask] = frame

    # Then chords on the chosen beats.
    beat_ticks = chord_every * ticks_per_quarter
    # The frame nearest each beat, so a melody played slightly off the grid still gets chords.
    step = frame_tick(1) - frame_tick(0)
    for frame, word in enumerate(melody):
        if not word or (frame_tick(frame) + step // 2) % beat_ticks >= step:
            continue
        top = max(bells(word))
        if add_harmony(frame, top, max_bells - len(bells(out[frame]))):
            stats["chords"] += 1
    return out, stats


def strum(words, frames, pattern, chord_at, frame_tick, strum_bells, min_gap):
    """Strike the harmony's chord on the 'x' frames of PATTERN (repeated from
    frame 0) for the first FRAMES frames. Each hit takes up to STRUM_BELLS free
    chord tones, highest first, so quick hits rotate through the chord's bells."""
    hits = 0
    for frame in range(frames):
        if pattern[frame % len(pattern)] != "x":
            continue
        chord = chord_at(frame_tick(frame))
        window = words[max(0, frame - min_gap):frame] + words[frame + 1:frame + 1 + min_gap]
        taken = set()
        for midi in sorted(MASK_BY_MIDI, reverse=True):
            mask = MASK_BY_MIDI[midi]
            if (len(taken) < strum_bells and midi % 12 in chord and midi % 12 not in taken
                    and not words[frame] & mask and not any(word & mask for word in window)):
                words[frame] |= mask
                taken.add(midi % 12)
        hits += bool(taken)
    return hits


def analyze(path):
    """Print what's needed to pick tracks and a transposition."""
    ticks_per_quarter, tracks, tempos = mtb.parse_midi(path)
    quarter_us = tempos[0][1] if tempos else 500_000
    print(f"{path.name}: {ticks_per_quarter} ticks/quarter, {60e6 / quarter_us:.1f} BPM "
          f"(eighth note = {quarter_us / 2000:.0f} ms)"
          + ("; tempo changes" if len({t for _, t in tempos}) > 1 else ""))
    bell_midis = set(MASK_BY_MIDI)
    for track in tracks:
        if not track.notes:
            continue
        notes = [note.note for note in track.notes]
        ticks = {note.tick for note in track.notes}
        chords = sum(1 for tick in ticks if sum(n.tick == tick for n in track.notes) > 1)
        grid = next((label for div, label in ((2, "eighth"), (4, "sixteenth"), (3, "triplet"), (6, "triplet 16th"))
                     if all(tick % (ticks_per_quarter // div) == 0 for tick in ticks)), "finer than 16th")
        pcs = " ".join(NAMES[pc] for pc in sorted({n % 12 for n in notes}))
        print(f"\ntrack {track.number} {track.name.strip() or '(unnamed)'}: {len(notes)} notes, "
              f"{name(min(notes))}-{name(max(notes))}, first at quarter {min(ticks) / ticks_per_quarter:g}, "
              f"{chords} chord onsets, {grid} grid")
        print(f"  pitch classes: {pcs}")
        # Rank shifts: notes landing exactly on a bell, then smallest shift.
        fits = []
        for shift in range(-12, 49):
            exact = sum(n + shift in bell_midis for n in notes)
            fits.append((exact, -abs(shift), shift))
        for exact, _, shift in sorted(fits, reverse=True)[:3]:
            print(f"  --transpose {shift:+d}: {exact}/{len(notes)} notes land on a bell "
                  f"({name(min(notes) + shift)}-{name(max(notes) + shift)})")


def main():
    if "--analyze" in sys.argv:
        analyze(Path(next(a for a in sys.argv[1:] if a != "--analyze")))
        return 0
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("midi", type=Path)
    parser.add_argument("--analyze", action="store_true",
                        help="list tracks, tempo, ranges and the best --transpose values, then exit")
    parser.add_argument("--melody-track", type=int, required=True)
    parser.add_argument("--harmony-track", type=int, required=True)
    parser.add_argument("--transpose", type=int, default=0,
                        help="semitones, applied to the melody and the harmony's chords")
    parser.add_argument("--frames-per-quarter", type=float, default=2)
    parser.add_argument("--step-ms", default="auto",
                        help="frame time in ms, or 'auto' to follow the MIDI tempo (default)")
    parser.add_argument("--intro", action="store_true",
                        help="before the melody starts, use the harmony track's top voice as the melody")
    parser.add_argument("--strum-intro", metavar="PATTERN",
                        help="before the melody starts, strike the harmony's chords on the 'x' frames of "
                             "PATTERN, one character per frame, e.g. x......x..xx..x. for a bar of 16ths")
    parser.add_argument("--strum-bells", type=int, default=2, help="bells per --strum-intro hit (default 2)")
    parser.add_argument("--chord-every", type=float, default=2, help="quarter notes between chords")
    parser.add_argument("--max-bells", type=int, default=3)
    parser.add_argument("--min-gap-ms", type=float, default=400,
                        help="time before a bell may be struck again (default 400); 0 allows a bell "
                             "on consecutive frames")
    parser.add_argument("--trailing-rest-frames", type=int, default=10)
    parser.add_argument("--output-header", type=Path)
    parser.add_argument("--output-wav", type=Path)
    args = parser.parse_args()

    ticks_per_quarter, tracks, tempos = mtb.parse_midi(args.midi)
    if args.step_ms == "auto":
        if len({tempo for _, tempo in tempos}) > 1:
            print("warning: the MIDI changes tempo; using its first tempo for every frame")
        quarter_us = tempos[0][1] if tempos else 500_000
        step_ms = round(quarter_us / 1000 / args.frames_per_quarter)
    else:
        step_ms = float(args.step_ms)
    min_gap = max(0, math.ceil(args.min_gap_ms / step_ms - 1e-9))
    # min_gap counts the frames between strikes, so a bell repeats every min_gap + 1 frames at the soonest.
    print(f"{step_ms:g} ms frames; a bell repeats after {min_gap + 1} frames ({(min_gap + 1) * step_ms:g} ms) at the soonest")

    melody_track = tracks[args.melody_track]
    harmony_track = tracks[args.harmony_track]
    if args.intro:
        start = min(note.tick for note in melody_track.notes)
        intro = [note for note in harmony_track.notes if note.tick < start]
        melody_track = mtb.MidiTrack(melody_track.number, melody_track.name,
                                     tuple(sorted(intro, key=lambda note: note.tick)) + melody_track.notes)
        print(f"Intro: {len({note.tick for note in intro})} harmony onsets before the melody")
    melody, substitutions, _, _ = mtb.convert_track(
        melody_track, ticks_per_quarter, tempos, step_ms, args.trailing_rest_frames,
        "highest", args.frames_per_quarter, args.transpose)
    if substitutions:
        print(f"warning: {sum(substitutions.values())} melody notes snapped to the nearest bell")
    folded = sum(1 for note in melody_track.notes
                 if not mtb.AVAILABLE_MIDI[0] <= note.note + args.transpose <= mtb.AVAILABLE_MIDI[-1])
    if folded:
        print(f"warning: {folded} melody notes fall outside C6-E7 after --transpose and were folded")

    first_tick = min(note.tick for note in melody_track.notes)
    lead = 0
    if args.strum_intro:
        # Start the frames where the harmony starts, and strum until the melody comes in.
        harmony_start = min(note.tick for note in harmony_track.notes)
        lead = max(0, round((first_tick - harmony_start) * args.frames_per_quarter / ticks_per_quarter))
        melody = [0] * lead + melody
        first_tick -= round(lead * ticks_per_quarter / args.frames_per_quarter)

    def frame_tick(frame):
        return first_tick + round(frame * ticks_per_quarter / args.frames_per_quarter)

    chord_at = chords_by_tick(harmony_track, 2 * ticks_per_quarter, args.transpose)
    words, stats = arrange(melody, chord_at, frame_tick, ticks_per_quarter,
                           args.chord_every, args.max_bells, min_gap)
    if lead:
        hits = strum(words, lead, args.strum_intro, chord_at, frame_tick,
                     min(args.strum_bells, args.max_bells), min_gap)
        print(f"Strum intro: {hits} hits over {lead} frames ({lead * step_ms / 1000:.1f} s)")

    sizes = [len(bells(word)) for word in words if word]
    print(f"{len(words)} frames, {len(sizes)} strikes: "
          + ", ".join(f"{sizes.count(n)} x {n} bell{'s' * (n > 1)}" for n in range(1, args.max_bells + 1)))
    print(f"{stats['masked']} melody repeats carried by a chord, {stats['unmasked']} left ringing "
          f"without one, {stats['chords']} beat chords; {len(words) * step_ms / 1000:.1f} s")

    if args.output_header:
        mtb.write_header(args.output_header, words, args.midi, melody_track, step_ms)
        print(f"Wrote {args.output_header}")
    if args.output_wav:
        rom_player.render_wav(words, args.output_wav, step_ms, 44100, 0.22)
        print(f"Rendered WAV preview to {args.output_wav}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
