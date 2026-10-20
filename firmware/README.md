# Firmware

This directory contains Arduino firmware for the Open Jingle Bells project.

## Components

### Transmitter
The central controller that reads note data from the ROM dump and broadcasts it to all player units using ESP-NOW.

### Player
Firmware for individual bell units (`player/`). Strikes bells from ESP-NOW packets in the transmitter's format (channel 6) or from line commands over the native USB serial port: `strike <word>`, `note <name> [...]`, `pulse [ms]`, `notes`, `status`, `help`. Each command gets an `OK` or `ERR` reply. Words use the ROM bit order; `bell_pinout.csv` lists each bell's note (C6–E7), ROM bit, edge pin and GPIO. The default pin map is the merrie-minstrel-choir board. Build with `--build-property compiler.cpp.extra_flags=-DPLAYER_BOARD` for the merrie-minstrel-player board, which also adds `power on|off`. Build with `-b esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M` so `Serial` is the USB port. `player/play_scale.py --port /dev/ttyACM1` replays the ROM intro scale (high to low, then back up) as a check.

### ROM Dumper
Utility to capture the original ROM data from a Ye Merrie Minstrel cartridge. The Arduino sketch reads the 12-bit parallel data bus on each clock edge from the ROM's internal oscillator and streams it as hex over serial. A companion Python script (`rom-recorder.py`) captures the serial output and saves it to a binary file.

## Requirements

- Arduino CLI or Arduino IDE
- ESP32 board support package
- Python 3 with `pyserial` (for ROM dumper)

## Compile and Flash

Run commands from within the `transmitter/`, `player/`, or `rom-dumper/` directory.

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

## Configuration

### Channel
Both transmitter and player must use the same WiFi channel. Default is channel 6, configured via the `channel` variable.
