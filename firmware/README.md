# Firmware

This directory contains Arduino firmware for the Open Jingle Bells project.

## Components

### Bell unit
This directory is the bell-unit firmware, an Arduino sketch (`firmware.ino`). It replaces the old ESP-NOW `player.ino` and `transmitter.ino` sketches, which are in git history. It targets the open-jingle-bells-choir board, and `-DPLAYER_BOARD` builds it for the player board. The plan is in `docs/firmware-plan.md`. It plays a compiled-in song library, joins Wi-Fi through a setup access point, serves a control page, and plays in unison with other units on the same network over UDP.

- **Songs:** run `ojb-build-songs` (from `tools/`, see `tools/README.md`) to generate `firmware/songs.h` from `tools/roms/roms.csv`. The ROM headers, `roms.csv` and `songs.h` hold copyrighted ROM data, so git ignores them. On a fresh clone, dump your own cartridge, put the dump at `tools/roms/rom_dump_hex.h`, and run `ojb-split-rom roms/rom_dump_hex.h roms/` from `tools/`, then `ojb-build-songs`, before compiling.
- **Build and flash:** run `arduino-cli compile -u -p /dev/ttyACM0 -b esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=default_8MB` in `firmware/`. Compile and upload in one step. A bare `upload` flashes the last build, which may be the other board's pin map. The boot banner, `status` and the Health page name the board the build is for.
- **Setup:** an unconfigured unit opens an open network named `JingleBells-XXXX` with a captive portal, where you pick your Wi-Fi network. On the network the page is at `http://<name>.local/`, which defaults to `jinglebells-xxxx`. If joining fails for 20 s, the setup network comes back.
- **Group sync:** all units on the same network and sync group play together. Any unit's page, button or serial console controls the whole group; the unit you use becomes the conductor. The lowest-MAC unit keeps the group clock. Play starts 1 s after the press, so every unit can start at the same instant. A unit not on Wi-Fi plays alone.
- **Page:** play, stop, next, once/repeat/play-all, the song list, a bell test, and health. Health shows the group (each unit's sync state, round trip, jitter, late strikes, with links to their pages), playback lateness, Wi-Fi, heap and last reset. Setup covers Wi-Fi, the unit name, strike length, the setup channel, the sync group and a per-unit strike offset.
- **Button:** S1 (BOOT) toggles play/stop with a short press after boot. Holding it 5 s forgets Wi-Fi. The LED blinks slowly while the unit sits in setup mode.
- **Serial:** `strike <word>`, `note <name> [...]`, `pulse [ms]`, `notes`, `status`, `help`, `songs`, `play [n]`, `stop`, `next`, `mode single|loop|all`, `wifi [ssid pass|forget]`, `name [name]`, `sync` (roles, clock, peers) and `drop <pct>` (discards received sync packets, for testing).
- **Watching from a laptop:** `ojb-sync-monitor` decodes the HELLO/STATE broadcasts on UDP 47800 (`--summary` for a unit table). `--time <ip>` measures the round trip to the clock master.

### Song and bell tools
The Python tools live in the top-level `tools/` package: the song library (`tools/roms/`), new arrangements (`tools/songs/`), song making, the USB serial bell tools, `ojb-build-songs` and `ojb-sync-monitor`. See `tools/README.md` to install them. The serial tools talk to a unit running this firmware: each command gets an `OK` or `ERR` reply, and words use the ROM bit order.

`bell_pinout.csv` lists each bell's note (C6–E7), ROM bit, edge pin and GPIO.

### ROM Dumper
Utility to capture the original ROM data from a Ye Merrie Minstrel cartridge. The Arduino sketch reads the 12-bit parallel data bus on each clock edge from the ROM's internal oscillator and streams it as hex over serial. A companion Python script (`rom-recorder.py`) captures the serial output and saves it to a binary file.

## Requirements

- Arduino CLI or Arduino IDE
- ESP32 board support package
- Python 3 with `pyserial` (for ROM dumper)

## Compile and Flash

Run commands from within `firmware/`; use the command under Bell unit above.

### Compile
```bash
arduino-cli compile -b esp32:esp32:esp32s3
```

### Flash
```bash
arduino-cli upload -b esp32:esp32:esp32s3 -p /dev/cu.usbserial-0001
```

### Monitor Serial Output
```bash
arduino-cli monitor -p /dev/cu.usbserial-0001 -c baudrate=115200
```

Or use screen:
```bash
screen /dev/cu.usbserial-0001 115200
```

## ROM Dumping

1. Flash `rom-dumper.ino` to an ESP32
2. Connect the ESP32 to the ROM cartridge's 12-bit data bus and clock pin
3. Run the Python recorder script:
   ```bash
   python rom-recorder.py
   ```
4. Play through the entire song sequence on the original unit
5. Press Ctrl+C to stop recording and save to `dump.bin`
