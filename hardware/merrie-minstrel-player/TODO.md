# merrie-minstrel-player — TODO before ordering

Status: schematic done, board placed and fully routed (32.5 × 60 mm, 2-layer, OSH Park rules).
DRC reports 0 unconnected items.

## Must do

- [x] **Silkscreen cleanup.** 86 silk overlap / silk-over-copper warnings are now 0. Uncrowded references were moved
      next to their parts on F.SilkS. Crowded ones are on **B.SilkS**, mirrored and centred under their part:
      R9–R18, Q1, Q2, R7, R8, C10, C11, C1, C2, C3, C7, C9, C6, C12, C14, C16, U5–U7, D2, D3, S1, S3.
      J2's reference and J1's `*` markers are on F.Fab. 5 silk-edge warnings remain from footprint artwork (see below).
- [ ] **Verify gold-finger orientation against a real music module.** Which face carries the odd pins
      (currently F.Cu = odd, B.Cu = even) and which end is pin 1 (currently the left end, viewed from the front).
      A mirrored pinout would make the board unusable. Footprint: `player.pretty/CardEdge_2x12_P2.54mm_GoldFinger`.
- [ ] **Verify the card-edge fit.** The tab is 32.5 mm wide with fingers 1.78 × 7.0 mm at 2.54 mm pitch, sized from
      the EDAC 395 drawing (33.02 mm slot, 8.38 mm depth). Check against the module's real socket, including any
      keying slot, and decide on edge bevelling and hard gold for the fingers.
- [ ] **Final DRC pass in KiCad** after the silkscreen work. Refill the zones first.
- [ ] **Generate fab outputs** (Gerbers and drill files) for OSH Park.

## Known DRC items accepted as-is (review before ordering)

- U1 (ESP32-S3-WROOM-1) thermal vias are 0.2 mm drill, below the 0.254 mm minimum (12 errors). They're built into the stock
  footprint. OSH Park may plate or enlarge them; a local footprint copy with larger or no thermal vias would clear the errors.
- J1 (USB-C, vendor footprint) pads sit 0.17–0.20 mm from its own NPTH holes (4 errors). Inherent to the footprint.
- Starved thermal on C10 pad 2 (GND, F.Cu pour): one spoke instead of two. The pad is already connected by a track.
- U1 footprint differs slightly from the KiCad 10 library copy (warning only).
- U2 (AZ1117, vendor `SOT-223_DIO` footprint): the tab pad 4 is unconnected, same as on merrie-minstrel-choir.
- Silk edge clearance (5 warnings) comes from footprint artwork. The U1 antenna outline and J1 USB-C shell outline
  overhang the edge by design, and U2's pin-1 dot sits at x = 99.95. The fab clips silk at the board edge.
- Schematic parity: 50 `footprint_symbol_field_mismatch` warnings, all for the 'Description' field (empty on the PCB).
  "Update PCB from Schematic" clears them.
- Schematic ERC: one false positive. The vendor USB-C symbol types both of its GND pins as "power output".

## BOM / assembly

- [ ] Mark R9–R18 (0603 bypass resistors, value "0R DNP") as **DNP** in their symbol properties so they're
      excluded from the BOM and position files.
- [ ] Confirm the TXS0108E parts on hand are the TSSOP-20 (PW) package. That's the footprint used.
- [ ] Confirm the ESP32-S3-WROOM-1 is the **N8 (no PSRAM)** variant. IO35/IO36/IO37 carry bell lines.

## Firmware (new transmitter / player sketch)

- [ ] Write the module-reader firmware using the GPIO map in the schematic note:
      BELL1 IO7, BELL2 IO16, BELL3 IO18, BELL4 IO48, BELL5 IO36, BELL6 IO38, BELL7 IO15, BELL8 IO17, BELL9 IO8,
      BELL10 IO35, BELL11 IO37, BELL12 IO39; P1 IO5, P2 IO6, P11 IO9, P12 IO10, P13 IO11, P14 IO12, P15 IO13,
      P16 IO14, P17 IO21, P18 IO47; LVL_OE IO4, MOD_PWR_EN IO40, LED IO41, PLAY IO42, BOOT IO0. IO1/IO2 are spare.
- [ ] Power-up sequence: keep LVL_OE low → enable MOD_5V → wait → raise LVL_OE.
- [ ] Probe unknown edge pins 1, 2 and 11–18 to identify clock, select and RC functions.

## Notes

- Routing came from Freerouting via `route/player-r12.ses`. The DSN export is post-processed: boundary inset 0.5 mm,
  NPTH keep-outs, J1 pin-name fix, and 0.21 mm default width. Re-routing needs the same steps.
- Close KiCad completely before editing netclasses or rules in the `.kicad_pro`. Otherwise KiCad overwrites them on save.
