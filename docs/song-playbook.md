# Song playbook: MIDI → bells, checked by microphone

A runbook for an agent arranging a new song for the 12 bells. Follow it top to
bottom. Background, measurements and the reasons behind each rule are in
[composing.md](composing.md). Frosty took this path:
`tools/songs/frosty_chords_rom.h`. Starting from a recording
instead of a MIDI? Make the MIDI first with [audio-to-midi.md](audio-to-midi.md).

**Done means:** a ROM header in `tools/songs/`, and a full microphone
take of it with **0 wrong**, **≤ 2 missed + extra**, and **≥ 80% ok**. Weak
strikes are acceptable: the bell rang but blurred. Also a results row in
`composing.md`, and a recording path the user can listen to. The ear is the
final judge, so always give the user the WAV.

## Setup facts (verify, don't research)

| Thing | Value |
|---|---|
| Bell board | ESP32-S3 on USB `/dev/ttyACM1`, running `firmware/` (choir-board build) |
| Check it | `minstrel-play-scale` → `PASS: 23 strikes acknowledged` |
| If it isn't running minstrel | `cd firmware && arduino-cli compile -u -p /dev/ttyACM1 -b esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=default_8MB` |
| Other units | Keep the board off Wi-Fi or in its own sync group, or a song started on another unit plays over your test |
| Microphone | System default input, the Logitech C925e webcam. The Jabra records silence. |
| Python | numpy is installed; pyserial is not, and the scripts don't need it |
| Recording latency | About 2 s, handled automatically |
| Other serial ports | `/dev/ttyUSB0` is an unrelated ESP8266; don't flash it |

**Bells:** C6 D6 E6 F6 F#6 G6 A6 A#6 B6 C7 D7 E7, at most 3 struck together.
There's no C#, D# or G#. Keys that fit: C major, G major (F#), F major (A#),
and their relative minors.

## 1. Inspect the MIDI

```bash
minstrel-harmonize SONG.MID --analyze
```

For each track it prints the range, pitch classes, chord count, rhythmic grid,
start time, and the three best `--transpose` values.

- **Melody track:** usually 0 chord onsets, a singing range, and a name like
  "Solo", "Melody", "Lead" or "Vocal". It often starts later than the
  accompaniment.
- **Harmony track:** a piano, guitar or strings track with many chord onsets.
  Its range doesn't matter, because only its chord note names are used.
- **`--transpose`:** pick the melody's top suggestion, ideally N/N notes on a
  bell. Anything under about 95% means notes get snapped to wrong pitches;
  try another melody track or another shift.
- **Grid:** "eighth" works with the default `--frames-per-quarter 2`. For a
  sixteenth or triplet grid, use 4 or 3, and check the frame time it reports.
  Below about 120 ms, fast passages will blur.

No harmony track? Use the melody-only path at the end.

## 2. Arrange

```bash
minstrel-harmonize SONG.MID \
  --melody-track M --harmony-track H --transpose T --intro \
  --output-header tools/songs/<song>_rom.h \
  --output-wav media/compositions/<song>.wav
```

Read what it prints:

- `NNN ms frames`: taken from the MIDI tempo. The header records it, and
  `build_songs.py` and `bell_listen.py` read it from there.
- `warning: ... snapped` or `... folded`: the transposition is wrong. Go back
  to step 1. With `--intro`, the folded count also includes the harmony
  track's intro notes. Re-run without `--intro`: if the warning goes away,
  the melody is fine.
- `Intro: N harmony onsets`: only if the accompaniment starts first. Drop
  `--intro` if the intro sounds wrong without its melody instrument.
- `X melody repeats carried by a chord, Y left ringing`: Y is fine. Those
  notes just sustain.
- Strike mix, e.g. `188 x 1 bell, 47 x 2 bells, 28 x 3 bells`: about 25–30%
  chords worked well for Frosty.

The rules it enforces, which you shouldn't undo by hand-editing:
- No bell is struck again within 400 ms (`--min-gap-ms`). Re-striking a
  ringing bell barely sounds and can choke it.
- A chord tone is never an octave from the melody or from a bell struck
  nearby. Otherwise it sounds like the melody dropping an octave.
- At most 3 bells per frame (`--max-bells`).

## 3. Validate on the bells

```bash
# Calibrate once per session, or whenever the mic or bells move (~25 s):
minstrel-bell-listen calibrate
# Play the whole song and score it (runs for the song's length + ~5 s):
minstrel-bell-listen rom tools/songs/<song>_rom.h
```

Run it in the foreground with a long timeout, e.g. 400000 ms for a 2-minute
song. The frame time comes from the header automatically. The script prints a
table and a summary line, and writes `media/recordings/<timestamp>-<song>_rom.wav`
and a `.json` next to it.

| Result | Meaning | Action |
|---|---|---|
| `ok` | Exactly the sent bells were heard | — |
| `weak` | Every sent bell rang, but its strike couldn't be separated from ringing | Acceptable unless it clusters, see step 4 |
| `WRONG` | Different bells were heard | Investigate: wiring, firmware, or the wrong header |
| `MISSED` | Nothing was heard | Check the bell isn't being re-struck too soon |
| `EXTRA` | A strike was heard that wasn't sent | Room noise; re-run once before acting |

Calibration check: every bell in `calibrate`'s table should land within about
±60 cents of nominal with little bleed. Otherwise move the mic closer and
recalibrate.

Scores vary by about ±4% between calibrations. To compare two arrangements,
`rescore` both JSON reports after one calibration, rather than comparing
numbers from different sessions:

```bash
minstrel-bell-listen rescore media/recordings/<take>.json
```

## 4. Iterate

Find where failures cluster with a short script over the JSON report. Each
entry in `strikes` has `sent_s`, `sent`, `heard` and `result`. Group the
non-ok entries by bell and by time since that bell last rang, using the
header's words and frame time. Then:

| Symptom | Fix |
|---|---|
| Fails cluster at short same-bell gaps | Raise `--min-gap-ms` (try 600) |
| Chords mostly weak, or the texture sounds busy | `--chord-every 4` (downbeats only) or `--max-bells 2` |
| Melody too sparse after repeats were dropped | Lower `--chord-every` so more attacks get chord support |
| G6 fails inside C-major chords | Analyzer limit: a C6 overtone sits on G6. Not an arrangement bug |
| Low C6/D6 also show up as C7 | Analyzer limit: overtone. Ignore |
| Song too fast or slow | `--step-ms N` overrides the MIDI tempo |
| Same failures after a re-run | Recalibrate, then `rescore` |

Each change means re-arranging and running another full take, a few minutes.
Stop when the done criteria are met; chasing the last few weak strikes isn't
worth melody changes.

## 5. Deliver

1. Keep the header at `tools/songs/<song>_rom.h`. Its second line
   states the frame time.
2. To add it to the units' library, copy the header into
   `tools/roms/`, add a row for it to `roms.csv` there
   (`song_title` and `file` are the columns that matter), run
   `minstrel-build-songs`, and reflash. Only do this if the
   user asks; every unit must then be reflashed to keep the libraries
   matching.
3. Add a row to "Results so far" in `composing.md`: the song, its source
   tracks, frames, strikes and chords, and the ok/weak/missed/wrong/extra
   counts. Note anything new you learned.
4. Tell the user the score and the WAV path, and ask them to listen.
5. Don't commit unless asked. MIDI files are the user's; leave them where
   they are.

## Melody only (no usable harmony track)

```bash
minstrel-midi-to-bells SONG.MID --track M --voice highest \
  --transpose T --frames-per-quarter 2 --step-ms <eighth-note ms from --analyze> \
  --avoid-repeats 2 --output-header tools/songs/<song>_rom.h
```

- Always pass `--transpose`. Without it, each note is folded into range on its
  own, which turns leaps into octave drops.
- `--avoid-repeats` counts frames, not ms. Use 2 at about 200 ms frames, so
  repeats land at least 400 ms apart.
- It moves a too-soon repeat to the other octave or a third. That can bring
  back octave jumps, which is why the chord path is preferred.

## Writing frames by hand

For a score with no MIDI, write the header directly: comment lines, then one
`0x...` word per frame, with bits in ROM order (`firmware/bell_pinout.csv`
maps notes to bits). Keep the same rules: no
bell re-struck within 400 ms, at most 3 bells per frame, no octave doubling
next to the melody. Put the frame time in the header's second line, as
"One 12-bit frame every N ms", so `bell_listen.py` plays it at the right
speed.
