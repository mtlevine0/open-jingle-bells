#!/usr/bin/env python3
"""Split a music-module ROM dump into one ROM header per song.

Songs in the dump are separated by runs of silent (0x000) frames. Runs of
--min-gap or more frames mark a boundary, but some songs have long rests of
their own (Joy to the World, Silent Night's two verses), so the song table
below lists which segments make up each song. The titles were identified by
decoding the melodies; the ROM order differs from the module's printed track
list in places, so the track number is recorded separately.

Each song keeps the silence that follows it on the ROM, so looping it keeps
the original spacing. Writes <out>/<module>_<pos>_<title>_rom.h per song and
<out>/roms.csv indexing them.

Usage (from tools/): minstrel-split-rom roms/rom_dump_hex.h roms/
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

MODULE = 801
MODULE_NAME = "christmas"

# (title, printed track number or None, segment indices), in ROM order.
SONGS = [
    ("Test Scale", None, [0, 1]),
    ("We Wish You A Merry Christmas", 1, [2]),
    ("O Come All Ye Faithful", 12, [3]),
    ("God Rest Ye Merry Gentlemen", 2, [4]),
    ("Joy to the World", 3, [5, 6, 7]),
    ("Angels We Have Heard on High", 4, [8]),
    ("Silent Night", 6, [9, 10]),
    ("Jingle Bells", 7, [11]),
    ("Here We Come a Caroling", 8, [12]),
    ("The First Noel", 9, [13]),
    ("Hark the Herald Angels Sing", 10, [14]),
    ("Away in a Manger", 11, [15]),
    ("Deck the Halls", 5, [16]),
    ("The Holly and the Ivy", 13, [17]),
    ("We Three Kings of Orient Are", 14, [18]),
    ("Bring A Torch", 15, [19]),
    ("While Shepherds Watched their Flocks", 16, [20]),
    ("Frosty the Snowman", 17, [21]),
    ("Angels from the Realm of Glory", 18, [22]),
    ("It Came Upon a Midnight Clear", 19, [23]),
    ("O Little Town of Bethlehem", 20, [24]),
    ("O Holy Night", 21, [25]),
    ("Twelve days of Christmas", 22, [26]),
    ("Have Yourself a Merry Little Christmas", 23, [27]),
    ("Come All Ye Shepherds", 24, [28]),
    ("Parade of the Tin Soldiers", 25, [29]),
]


def segments(words: list[int], min_gap: int) -> list[tuple[int, int, int]]:
    """Return (start, end of sound, end of trailing silence) per segment."""
    segs, i, start = [], 0, 0
    while i < len(words):
        if words[i]:
            i += 1
            continue
        j = i
        while j < len(words) and not words[j]:
            j += 1
        if j - i >= min_gap:
            if i > start:
                segs.append((start, i, j))
            start = j
        i = j
    return segs


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("dump", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--min-gap", type=int, default=11,
                        help="silent frames that split segments (default 11)")
    args = parser.parse_args()

    words = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", args.dump.read_text())]
    segs = segments(words, args.min_gap)
    used = sum(len(idx) for _, _, idx in SONGS)
    if len(segs) != used:
        raise SystemExit(f"Found {len(segs)} segments but the song table uses {used}")

    args.out.mkdir(exist_ok=True)
    rows = []
    for pos, (title, track, idx) in enumerate(SONGS):
        start, end = segs[idx[0]][0], segs[idx[-1]][2]
        frames = words[start:end]
        slug = re.sub(r"[^a-z0-9]+", "_", title.lower()).strip("_")
        name = f"{MODULE}_{pos:02d}_{slug}_rom.h"
        header = [
            f"// Module {MODULE} ({MODULE_NAME}), ROM position {pos}: {title}",
            f"// Extracted from {args.dump.name} frames {start}-{end - 1}"
            + (f"; CSV track {track}." if track else "; not in the CSV track list."),
            f"// One 12-bit frame every 200 ms; {len(frames)} frames total.",
        ]
        (args.out / name).write_text("\n".join(header + [f"0x{w:03X}," for w in frames]) + "\n")
        rows.append([MODULE, MODULE_NAME, pos, track or "", title, name, start, end - 1, len(frames)])
        print(f"{name}: {len(frames)} frames")

    with open(args.out / "roms.csv", "w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["module", "module_name", "rom_position", "csv_track_number",
                         "song_title", "file", "start_frame", "end_frame", "frames"])
        writer.writerows(rows)


if __name__ == "__main__":
    main()
