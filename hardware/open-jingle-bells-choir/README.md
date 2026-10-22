# open-jingle-bells-choir

ESP32-S3 receiver board that plugs onto the bell unit's card edge (J2, EDAC 395-024-559-212).

## Rev 2 (branch `choir-v2`)

### EN fix

On rev 1, R1 (10k) ran from EN to C3, and C3 went to GND. EN had no pull-up to +3.3V.
Rev 2 wires the standard Espressif RC:

- R1 10k: +3.3V to EN
- C3 1uF: EN to GND

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

The board is 53.7–80.5 × 61–98 mm (26.8 × 37 mm), down from 35 × 55 mm in rev 1 and
26.8 × 47 mm in the first rev 2 layout. What sets the limits:

- left edge: 0.5 mm outside the copper of J2's outer pin row
- top edge: the base of the module's antenna; the antenna hangs off the board
- bottom edge: J2's lowest pins (y 94.55) and the stack of rows under U1. **J1 moved up
  10 mm with it, so the enclosure's USB-C opening has to move too.**
- right edge: the ESP32 module and USB-C

### Placement: R and C on the back

Every resistor and capacitor is on the back, behind the part it serves. The back can't
use x < 60.7 mm, because J2's socket body covers that strip.

| Back side | Behind |
|-----------|--------|
| C7, C8 (3V3 decoupling), R1, C3 (EN RC) | U1's 3V3 and EN pins, under the module |
| R2, R5 | their LEDs D2, D3 |
| R6 (GPIO0 pull-up) | S1 (BOOT) |
| C9 (VBUS) | U4 |
| R3, R4 (CC pull-downs) | J1 |
| C1, C2 (LDO in/out) | U2 |

The top side has three rows under U1:
- row A: D2, D3 and J3, with J3 now horizontal along the module's bottom edge
- row B: S2, S1 and U4
- row C, on the bottom edge: U2 and J1

The UART pin labels on the back sit just above J3's pins.

### Smaller passives

LEDs D2/D3 are now 0805 and all resistors (R1–R5) are 0603, both hand-solder variants.
Capacitors stay 0805.

### USB routing

The J1/U4/C9 corner is hand-routed and locked (`route/usb_v3.py`):
- D− (4 mm, no vias) joins B7/A7 on a bar above the pads and runs straight up to U4 pin 3.
- D+ (12 mm, 2 vias) joins A6/B6 on a bar below the pads. It drops to B.Cu, runs up
  between C9's pads and comes back to F.Cu above U4 to reach pin 1.
- A via beside U4 pin 2 ties it to C9's GND pad on the back.
- J1's left VBUS pin gets its own via.

Freerouting routed U4 to U1 (IO19/IO20): USB_D+ is 29.6 mm and USB_D− 31.0 mm, with
4 vias each and 1.4 mm skew. They aren't a coupled pair any more. That's fine at USB full
speed (12 Mb/s), where the skew is far below a bit time and impedance isn't controlled
anyway.

### Routing

The board was re-routed with Freerouting using the scripts in `route/`:

1. `v2_layout.py` takes the **rev 1** board and the rev 2 netlist. It applies pad nets,
   flips J2, places parts, and sets the outline and pour.
2. Export the DSN with `pcbnew.ExportSpecctraDSN`, then run `post_dsn.py`. This strips
   the planes, insets the boundary 0.5 mm, renames J1's hyphenated pins and sets
   0.25/0.45 mm net classes.
   Before the export, `usb_pair.py` adds the hand-routed USB tracks (locked, so they go to
   Freerouting as fixed wiring).
3. Route with Freerouting. `import_ses.py` imports the SES, drops the zero-length stubs
   Freerouting leaves on fixed nets, widens any track necked below 0.1524 mm, and fills
   the zones.
4. `gnd_cleanup_drc.py` removes redundant GND traces. It keeps a removal only when
   DRC shows no new unconnected items, starved thermals or connection-width warnings.

The routed job (`route/choir-v2.dsn`, `.ses`) is kept locally; the repo ignores `*.dsn` and `*.ses`.

The shrunk layout replaced step 1 and the USB step:

1. `v3_layout.py` strips the routing, places parts top and back, and moves the outline and
   pours to y = 98. Run it with the project's `.kicad_pro` next to the input board, or
   pcbnew loads default design rules and saves them over the project's.
2. `usb_v3.py` adds the locked J1/U4/C9 routing (see above).
3. DSN export, `post_dsn.py`, Freerouting, `import_ses.py` and `gnd_cleanup_drc.py` as
   before. Cleanup removed 77 of 83 GND segments.
4. `silk_v3.py` moves each reference label to the nearest spot clear of pads and other
   silkscreen, at the board's minimum text size (0.8 mm).

Remaining DRC items, all from before this layout apart from the last one:
- U1's 0.2 mm via drills
- U1's silkscreen at the antenna edge
- library-mismatch notes
- the U1/J2 parity notes
- one starved-thermal warning: R4's GND pad has a single spoke into the back pour

With clearance re-enabled, the only clearance hits are the 11 inside J1's own pads.

### Schematic cleanup

- R2/R5 are 1.5k so the LEDs glow dimly for debugging: about 1 mA for red/yellow,
  about 0.4 mA for blue/white/green.
- R6 (10k, 0603) pulls GPIO0 up to +3.3V. S1 stays: it selects download mode at reset
  for flashing over J3, and works as a user button (active low) after boot. Firmware
  debounces it.
- C5 and C6, the caps across the buttons, are gone. C5 was in parallel with C3, and C3
  with R1 already filters EN.
- S1/S2 are XUNPU TS-1088-AR02016 (3.9 × 3 × 2 mm SMD tactile, LCSC C720477) on KiCad's
  `Button_Switch_SMD:SW_SPST_TS-1088-xR020`, the same switch the IV-9 numitron clock
  uses. The symbol is still named `1825966-1`.
- A note on the sheet says the IO35 LED needs a module without octal PSRAM (N8). N8R8 and
  N16R8 use IO35–IO37 for PSRAM.
- ERC is clean (0 errors, 0 warnings). U1's GND pins are `power_in` and J1's are `passive`
  in the vendor libraries, and PWR_FLAGs mark VCC and GND. Two wire stubs on the
  USB_D± labels were trimmed. The Bell_9 label moved off J2's GND symbol.
- The project now has its own `sym-lib-table` and `fp-lib-table`. `footprints.pretty`
  holds J1, U2 and J2 as placed on the board, exported from the PCB. The J2 symbol was
  rebuilt into `../vendor/edac_395-024-559-212/` with the same pin positions.

### Known items carried over from rev 1

- U1's footprint has 0.2 mm thermal-via drills, below the 0.254 mm minimum in board setup
  (12 DRC errors in both revisions).
- The 3D model paths for J1, J2 and U2 point at a macOS path, so they don't render here.
