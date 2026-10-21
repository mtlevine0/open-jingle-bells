# merrie-minstrel-choir

ESP32-S3 receiver board that plugs onto the bell unit's card edge (J2, EDAC 395-024-559-212).

## Rev 2 (branch `choir-v2`)

### EN fix

On rev 1, R1 (10k) ran from EN to C3, and C3 went to GND. EN had no pull-up to +3.3V.
Rev 2 wires the standard Espressif RC:

- R1 10k: +3.3V to EN
- C3 1uF: EN to GND (C5 0.1uF stays in parallel with S2)

### J2 moved to the bottom side

The socket now hangs under the board. Its slot, mounting holes and pin rows sit in the
same X/Y positions as rev 1, so the card enters from the same side. The board now sits
at a different height relative to the card, so check the enclosure stack-up.

Mounting the socket upside down turns it 180 degrees about the insertion axis. The
contact that touches card finger *n* is now socket pin **25 − n**. The schematic has been
re-pinned to match, so every net still reaches the same finger on the bell card:

| Signal | Rev 1 J2 pin | Rev 2 J2 pin |
|--------|-------------:|-------------:|
| Bell_1 / Bell_7  | 3 / 4   | 22 / 21 |
| Bell_2 / Bell_8  | 5 / 6   | 20 / 19 |
| Bell_3 / Bell_9  | 7 / 8   | 18 / 17 |
| GND / VCC        | 9 / 10  | 16 / 15 |
| Bell_4 / Bell_10 | 19 / 20 | 6 / 5   |
| Bell_5 / Bell_11 | 21 / 22 | 4 / 3   |
| Bell_6 / Bell_12 | 23 / 24 | 2 / 1   |

No firmware change is needed, because the ESP32 GPIO for each bell is unchanged.

J2 uses `card_edge_395024559212_noMH`, the rev 1 footprint without its two mounting
holes. The socket's mounting ears hang off the board edge and the socket is held by its
24 solder pins. Because the schematic symbol still has its MH1/MH2 pins (no-connect),
the parity check warns that pins 25 and 26 have no pad.

### Smaller outline

The board is 53.7–80.5 × 55–108 mm (26.8 × 53 mm), down from 35 × 55 mm, and stays
inside the rev 1 outline. What sets the limits:

- left edge: 0.5 mm outside the copper of J2's outer pin row
- top edge: the ESP32 module (its antenna is flush with this edge)
- bottom edge: the USB-C connector, which stays where it was for the enclosure opening
- right edge: the ESP32 module and USB-C

### Smaller passives

LEDs D2/D3 are now 0805 and all resistors (R1–R5) are 0603, both hand-solder variants.
Capacitors stay 0805.

### Routing

The board was re-routed with Freerouting using the scripts in `route/`:

1. `v2_layout.py` takes the **rev 1** board and the rev 2 netlist. It applies pad nets,
   flips J2, places parts, and sets the outline and pour.
2. Export the DSN with `pcbnew.ExportSpecctraDSN`, then run `post_dsn.py`. This strips
   the planes, insets the boundary 0.5 mm, renames J1's hyphenated pins and sets
   0.25/0.45 mm net classes.
3. Route with Freerouting, then `import_ses.py` imports the SES and fills the zones.
4. `gnd_cleanup_drc.py` removes redundant GND traces. It keeps a removal only when
   DRC shows no new unconnected items, starved thermals or connection-width warnings.

The routed job (`route/choir-v2.dsn`, `.ses`) is kept locally; the repo ignores `*.dsn` and `*.ses`.

### Known items carried over from rev 1

- U1's footprint has 0.2 mm thermal-via drills, below the 0.254 mm minimum in board setup
  (12 DRC errors in both revisions).
- R2/R5 are 68 Ω in the schematic but 220 Ω on the PCB.
- The 3D model paths for J1, J2 and U2 point at a macOS path, so they don't render here.
