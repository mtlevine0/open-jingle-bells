# Composing for the bells

How to turn music into a ROM song for the 12 bells, and how to check by
microphone that the bells actually play it. Started 2026-10-19 with "Frosty
the Snowman"; updated 2026-10-20 with chords, the piano intro and real tempo.

To arrange a new song step by step, follow [song-playbook.md](song-playbook.md).
This document holds the background and measurements behind it.

## The ROM format

A song is a list of 12-bit words, one per frame. Each set bit strikes one
bell, and `0x000` is a rest. Up to 3 bells may be struck together.

The original ROM plays a frame every 200 ms, but our songs don't have to. A
generated header states its frame time in its second line ("One 12-bit frame
every 222 ms"). `bell_listen.py` and `build_songs.py` read it from there.

Bits follow the original ROM's order, which is not the pitch order:

| Bell (high to low) | Note | ROM bit | Mask  | Measured Hz | Nominal Hz |
|--------------------|------|---------|-------|-------------|------------|
| 1                  | E7   | 3       | 0x008 | 2670        | 2637       |
| 2                  | D7   | 7       | 0x080 | 2410        | 2349       |
| 3                  | C7   | 6       | 0x040 | 2116        | 2093       |
| 4                  | B6   | 5       | 0x020 | 1994        | 1976       |
| 5                  | A#6  | 4       | 0x010 | 1930        | 1865       |
| 6                  | A6   | 8       | 0x100 | 1775        | 1760       |
| 7                  | G6   | 9       | 0x200 | 1573        | 1568       |
| 8                  | F#6  | 10      | 0x400 | 1507        | 1480       |
| 9                  | F6   | 11      | 0x800 | 1425        | 1397       |
| 10                 | E6   | 0       | 0x001 | 1359        | 1319       |
| 11                 | D6   | 1       | 0x002 | 1196        | 1175       |
| 12                 | C6   | 2       | 0x004 | 1066        | 1047       |

The original ROM opens with all 12 bells struck high to low in this order.
That run is the reference for the mapping. `firmware/bell_pinout.csv` adds
each bell's edge pin and GPIO. The measured pitches come from calibration
(below). Every bell is 5–60 cents sharp of nominal, and A#6 and E6 are the
furthest off.

The range is C6–E7. It has no C#6, D#6 or G#6, and above C7 there are only
D7 and E7. Music in C major (or F, using A#) fits best. Only C, D and E exist
in two octaves.

## Tools

| Tool | What it does |
|------|--------------|
| `minstrel-midi-to-bells` | Converts one MIDI track to a ROM header and a synthesized WAV preview |
| `minstrel-harmonize` | Arranges a melody track with chords from a harmony track, at the MIDI's own tempo |
| `minstrel-rom-player` | Analyzes the original ROM dump and renders WAV previews |
| `firmware/` | Bell firmware: plays its song library, and words from USB serial (`strike 0x200`, `note G6`, `pulse 40`) |
| `minstrel-play-scale` | Quick USB check: plays the ROM intro scale |
| `minstrel-bell-listen` | Plays on the real bells, records with a microphone, and reports what was heard |

## Workflow

### Melody with chords (current approach)

```bash
minstrel-harmonize ~/Downloads/FROSTY.MID \
  --melody-track 2 --harmony-track 3 --transpose 24 --intro \
  --output-header tools/songs/frosty_chords_rom.h \
  --output-wav media/compositions/frosty_chords.wav
minstrel-bell-listen rom tools/songs/frosty_chords_rom.h
```

- `--transpose` shifts the whole melody so it lands inside C6–E7. Pick a shift
  that needs no folding; the script warns about notes it had to fold.
- The frame time follows the MIDI tempo: one frame per eighth note with the
  default `--frames-per-quarter 2`, so 222 ms at Frosty's 135 BPM. `--step-ms`
  overrides it.
- `--intro` plays the harmony track's top voice as the melody until the
  melody track starts. Frosty's piano has a 17-chord intro before the tune.
- `--chord-every` sets how often chords go under the melody, in quarter notes.
  The default is 2, beats 1 and 3.
- `--max-bells` (3) and `--min-gap-ms` (400) set the limits below.

How it arranges:
1. When the melody repeats a bell within `--min-gap-ms`, the re-strike is
   dropped and the bell keeps ringing. Chord tones underneath supply the new
   attack, so the rhythm survives without an octave jump.
2. On each chord beat, up to two chord tones go under the melody. They come
   from the harmony track's latest chord, are at least a third apart, and use
   bells that are free.
3. A chord tone is never the melody's own note in another octave, and never
   an octave from any bell struck within the gap either side. Otherwise it
   would sound like the melody leaping an octave.

### Melody only

```bash
# 1. Find the melody track
minstrel-midi-to-bells song.mid --list-tracks

# 2. Convert it (2 frames per quarter = one eighth note per 200 ms frame)
minstrel-midi-to-bells song.mid --track 2 --voice highest \
  --frames-per-quarter 2 --step-ms 200 --trailing-rest-frames 10 \
  --avoid-repeats 2 \
  --output-header tools/songs/song_rom.h \
  --output-wav media/compositions/song.wav

# 3. Calibrate once per room or microphone position (~25 s)
minstrel-bell-listen calibrate

# 4. Play it on the bells and check it
minstrel-bell-listen rom tools/songs/song_rom.h --frames 9999
```

Without `--transpose`, the converter moves each note into the bell range one
octave at a time, on its own, then snaps any missing pitch to the nearest
bell. Folding notes separately breaks leaps: Frosty's C4 and C5 both landed on
C6, so "C' B C' D'" played as C6 B6 C6 D6, with 33 octave drops across the
song. Use `--transpose` (+24 for Frosty) whenever one shift fits the melody.

`--frames-per-quarter` quantizes on the musical grid rather than the MIDI
tempo. With 2 frames per quarter on 200 ms frames, the song plays at 150 BPM.
Use 3 for triplet-heavy material, and `--step-ms` for the real tempo.

## Validating with the microphone

`bell_listen.py` drives the player over USB (`/dev/ttyACM1`) and records the
system default input through `parecord`. Here that is the Logitech C925e
webcam mic; the Jabra headset records silence. It needs numpy.

- `calibrate` strikes each bell alone twice. It learns each bell's real
  pitch, how loud a normal strike is, and how much each bell bleeds into the
  other bells' frequency bands. It saves this to
  `tools/bell_calibration.json`.
- `scale`, `rom` play a sequence while recording. Each sent strike gets one
  of these results:
  - **ok**: exactly the sent bells were heard.
  - **weak**: the sent bell got louder on cue, but its strike couldn't be
    separated from the bell still ringing. You can hear it, but it's blurred.
  - **WRONG**: different bells were heard.
  - **MISSED**: nothing was heard.
  - **EXTRA**: a strike was heard that wasn't sent.
- `rescore <report.json>` re-checks a saved take, for example after
  recalibrating or changing the analysis, without striking the bells again.
- `analyze <wav>` lists the strikes heard in any recording.
- `--pulse MS` sets the firmware's strike length before playing.

Each take saves a WAV and a JSON report in `media/recordings/`. The
recording runs about 2 s behind the strike commands; the script finds that
latency by lining the sent pattern up with the audio.

How the analysis works: it takes a short-time FFT (4096 points, 10 ms hop)
and measures each bell's level in a ±1.2% band around its calibrated pitch.
A strike is a band rising above its own peak over the previous 250 ms. A
bell's natural decay and beating never do that, but a new strike does. The
rise in every band is then split among the bells with a non-negative
least-squares fit against the calibrated bleed patterns. Each bell is
judged against its own calibrated strike. It counts as heard if its rise is at
least 30% of that, and at least 35% of the strongest bell's relative rise. At
first this used raw microphone level, which let the loud F6 (about 5× D7 at
the mic) hide the melody in D7+A6+F6 chords.

Known limits:
- C6's first overtone falls in the C7 band, and another lands almost exactly
  on G6 (1066 × 1.48 ≈ 1573 Hz). So C6 can show up as C7, and G6 is hard to
  separate inside C-major chords even when it clearly rang.
- Scores move by about ±4% between calibrations (the same take scored 208 and
  221 of 263). Compare arrangements against one calibration, using `rescore`.

## What the bells taught us

**A bell struck again while it is still ringing barely sounds and can choke
itself.** This is the main constraint on arrangements. In the plain Frosty
conversion:

| Frames since the same bell last rang | Not clean / total |
|---|---|
| 1 (200 ms) | 11 / 14 |
| 2 (400 ms) | 11 / 27 |
| 3–4        | 3 / 24 |
| 5 or more  | 1 / 193 |

In one case (Rudolph, G6 at 7.6 s) the second strike made the level drop
instead of rise; the striker seems to damp the ringing bell. Changing the
strike pulse doesn't help: 15, 25, 40 and 70 ms gave inconsistent results
on a repeated-note test, so the firmware stays at 40 ms.

`midi_to_bells.py --avoid-repeats N` handles this. When a note repeats the
same bell within N frames, it moves to:

1. the same note in the other octave, if that bell exists (C, D, E); otherwise
2. a third below or above that is in the song's key;
3. or, if no such bell is free, it's dropped and the first strike keeps ringing.

A replacement bell must not have rung in the previous N frames or be due in
the next N.

Other observations:
- The low bells (C6, D6, F6, G6) are the loudest and ring longest, so they
  blur most. Repeats of them need the most spacing.
- Two-bell chords (Rudolph's B6+G6) came through cleanly.

- Chords of two and three bells come through: the latest Frosty take heard
  every bell of 54 of its 75 chords cleanly. In the other 21, every sent bell
  still rang ("weak").

## Results so far

| Song | Source | Frames | Strikes | Result |
|------|--------|--------|---------|--------|
| ROM intro scale (down and up) | `rom_dump_hex.h` | 23 | 23 | 22–23 ok, 0–1 weak |
| Rudolph, first 64 frames | `rudolph_rom.h` (score transcription) | 64 | 28 | 23 ok, 3 weak, 2 missed (all same-bell repeats) |
| Frosty, plain conversion | `FROSTY.MID` track 2 | 579 | 265 | 239 ok, 24 weak, 2 missed |
| Frosty, `--avoid-repeats 2` | same | 579 | 265 | 254 ok, 10 weak, 1 missed, 0 wrong, 0 extra |
| Frosty with chords, 200 ms | `harmonize.py`, tracks 2 + 3 | 579 | 246 (71 chords) | 213 ok, 32 weak, 1 missed, 0 wrong |
| Frosty with chords, intro, 222 ms | `harmonize.py --intro` | 613 | 263 (75 chords) | **221 ok, 41 weak, 1 missed, 0 wrong, 1 extra** |
| All the Small Things, intro, 197 ms | Blink-182 MIDI, tracks 1 + 6, `--transpose 29` | 790 | 222 (32 chords) | 203 ok, 18 weak, 1 wrong, 0 missed, 0 extra |
| All the Small Things, `--min-gap-ms 800` | same | 790 | 194 (22 chords) | **187 ok, 7 weak, 0 wrong, 0 missed, 0 extra** |

Frosty used track 2 ("T2 C2 Dr Solo"), which is only C D E F G A B C D, so
two octaves up fits C6–D7 with no substitutions. 34 repeated notes were
re-voiced. The 11 that are still not clean are all same-bell repeats 3–4
frames apart, mostly G6, C6 and D6. Re-voicing those too would change many
more melody notes for little gain.

The two chord rows used different calibrations. Re-scored against the same
fresh calibration, they come out at 203/246 (83%) for the 200 ms version and
221/263 (84%) for the 222 ms version with the intro.

All the Small Things is at 152 BPM, so its eighth-note frames are 197 ms and
the default 400 ms gap leaves 3 empty frames, so a bell re-strikes after 4 frames (788 ms) at the soonest. In the first
take, 13 of the 60 strikes that came less than 0.8 s after the same bell were
weak or wrong, against 6 of the 198 slower ones. The wrong one was F6 missing
from a chord 788 ms after its previous strike. Raising `--min-gap-ms` to 800
(5 frames) dropped 28 strikes but cleared it. At fast tempos, check the
same-bell gap in ms, not in frames. The guitar track is power chords, which
offer only roots and fifths, so only about 11% of strikes are chords. Also,
`--intro` counts the low guitar intro notes in its "folded" warning (105
here): the vocal alone fits at +29. Run once without `--intro` to tell the two
apart.

In the latest take the weak strikes cluster on two bells. C7 struck again
within 700 ms fails 6 of 13 times. G6 fails 6 of 11 at 700–1000 ms, and 9 of
38 even after 1.5 s, which is mostly the C6 overtone problem. Making G6 wait
longer would drop melody notes like the second G in "G E F G", so the
arrangement accepts these as audible but blurred.

To play a song on the units, add it to the library: copy the header into
`tools/roms/`, add a row to `roms.csv`, run
`minstrel-build-songs` and reflash every unit.

## Ideas to try next

- Per-bell gaps for chord tones only. G6 and C7 blur most, and longer gaps
  for them as harmony wouldn't touch the melody.
- Teach the analyzer bell overtones, measured per bell in calibration, so C6
  under G6 stops counting against G6.
- Calibrate with more repeats (`--repeats 4`) to steady the scores.
- Try `--chord-every 4` (downbeats only) and judge by ear whether chords every
  half bar are too busy.
