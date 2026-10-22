"""Repository locations the tools default to.

These resolve from this file, so they hold for an editable install
(`pip install -e tools`), which is the supported way to install the tools.
"""

from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2]
REPO = TOOLS.parent
FIRMWARE = REPO / "firmware"

ROMS = TOOLS / "roms"  # original module dump, split per song, and roms.csv
SONGS = TOOLS / "songs"  # new arrangements
ROM_DUMP = ROMS / "rom_dump_hex.h"
CALIBRATION = TOOLS / "bell_calibration.json"
PINOUT = FIRMWARE / "bell_pinout.csv"
SONG_TABLE = FIRMWARE / "songs.h"
MEDIA = REPO / "media"  # source audio and MIDI, renders, recordings (git-ignored)
