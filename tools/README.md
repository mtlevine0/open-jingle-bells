# open-jingle-bells-tools

Python tools for making songs, driving and listening to the bells, building
the firmware's song library and watching group sync.

## Install

Install editable from the repo root. The tools find the ROM data, the
calibration and `media/` relative to this checkout, so a non-editable
install isn't supported.

```bash
python -m venv .venv && . .venv/bin/activate
pip install -e tools            # core tools: standard library only
pip install -e 'tools[bells]'   # + numpy, for ojb-bell-listen
```

`ojb-audio-to-midi` needs PyTorch, so use Python 3.12, and ffmpeg on
PATH. basic-pitch pins a TensorFlow with no 3.12 wheels, so it goes in
without its dependencies (its ONNX runtime comes with the `audio` extra):

```bash
uv venv -p 3.12 .venv-audio && . .venv-audio/bin/activate
uv pip install -e 'tools[all]'
uv pip install --no-deps basic-pitch==0.4.0
```

Each command also runs as a module, e.g. `python -m open_jingle_bells_tools.build_songs`.
The serial tools use `termios`, so they run on Linux and macOS.

## Commands

| Command | What it does |
|---|---|
| `ojb-build-songs` | Builds `firmware/songs.h` from `roms/roms.csv` |
| `ojb-sync-monitor` | Decodes the units' HELLO/STATE broadcasts; `--time IP` probes the clock master |
| `ojb-play-scale` | Serial check: plays the ROM intro scale on a unit |
| `ojb-bell-listen` | Plays on the real bells, records with a microphone and scores what was heard (`[bells]`) |
| `ojb-rom-player` | Analyzes the original ROM dump and renders WAV previews |
| `ojb-split-rom` | Splits a module dump into one header per song plus `roms.csv` |
| `ojb-midi-to-bells` | Converts one MIDI track to a ROM header and a WAV preview |
| `ojb-harmonize` | Arranges a melody track with chords from a harmony track |
| `ojb-audio-to-midi` | Transcribes an audio recording to MIDI (`[audio]`) |

## Data

- `roms/`: the original module-801 dump (`rom_dump_hex.h`), its per-song
  headers and `roms.csv`, the library the firmware is built from.
- `songs/`: new arrangements.
- `bell_calibration.json`: per-bell spectra from `ojb-bell-listen calibrate`.

The ROM data is copyrighted, so git ignores the dump, every `*_rom.h`,
`roms.csv` and the generated `songs.h`. On a fresh clone, dump your own
cartridge, put the dump at `roms/rom_dump_hex.h`, then from `tools/`:

```bash
ojb-split-rom roms/rom_dump_hex.h roms/
ojb-build-songs
```

## ROM analysis and desktop playback

`ojb-rom-player` parses `roms/rom_dump_hex.h`, reports the identified
module-801 song regions, and synthesizes a WAV preview without third-party
Python packages.

```bash
ojb-rom-player analyze
ojb-rom-player render --song 1 --output /tmp/we-wish.wav
ojb-rom-player play --song 1
```

The desktop renderer defaults to the original ROM's 200 ms frame interval. Use
`--step-ms` before the subcommand to test another tempo. A unit energizes
each selected output for 40 ms by default; the rendered bell tone continues to decay after
that strike, as a physical bell would. The pitch mapping follows the measured
12-bell set documented at https://alnwlsn.com/z/misc/bells/bells.html.

### Convert a MIDI arrangement

`ojb-midi-to-bells` can quantize one MIDI track to the same 12-bit
ROM format. Notes are octave-folded into the physical C6-E7 range and pitches
that the bell set does not contain are snapped to the nearest bell.

```bash
ojb-midi-to-bells song.mid --list-tracks
ojb-midi-to-bells song.mid \
  --track 1 \
  --voice lowest \
  --frames-per-quarter 3 \
  --step-ms 150 \
  --output-header tools/songs/song_rom.h \
  --output-wav /tmp/song-preview.wav
```
