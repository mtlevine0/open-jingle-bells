# Merrie Minstrel Choir

An ESP32-based replacement for the "Ye Merrie Minstrel Caroling Christmas Bells" original ROM cartridge featuring wireless synchronization.  A single transmitter device can broadcast notes to any number of receivers creating a choir of christmas bells.

<p align="center">
  <img src="docs/assembled.JPG" alt="Assembled PCB" width="600">
</p>

## Overview

This project provides open-source firmware and hardware designs to replace the original ROM cartridge in Ye Merrie Minstrel handheld bell units. Using ESP-NOW wireless communication, multiple sets of bells can be synchronized to play together in a choir.  The PCB is meant to be a direct drop-in replacement to the original ROM cartridge, no modifications of the device is required.  I reccomend a power adapter with a larger current capacity than the original 200mA to compensate for the extra power comsumption of the ESP32.

I used [OSH Park](https://oshpark.com) to manufacture the PCBs.  

## Project Structure

```
├── firmware/
│   ├── transmitter/    # Central controller that broadcasts note data
│   ├── rom-dumper/     # Firmware for dumping ROM contents over a serial connection
│   └── receiver/       # Firmware for individual bell units
├── hardware/
│   └── merrie-minstrel-choir/  # KiCad PCB design files
└── docs/
```

## Hardware

The custom PCB is designed in KiCad and interfaces with the original bell unit via a 24-pin edge card connector, replacing the ROM cartridge.

<p align="center">
  <img src="docs/render.JPG" alt="PCB Render" width="500">
</p>

## Demo

[View operation video](https://youtube.com/shorts/tzY3T9JohGE?feature=share)

## Firmware

### Transmitter
The transmitter reads note data from a ROM dump and broadcasts it to all receiver units using ESP-NOW protocol with long-range mode enabled.

### Receiver
Each bell unit runs the receiver firmware, which listens for note data and drives the 12-bit parallel data bus to control the bell's solenoids.

### Rom Dumper
The original ROM contains copyrighted material.  I don't have permission to share the contents.  You can acquire your own ROM cartridge and dump the ROM using the Arduino program provided.


## Requirements

- ESP32 development boards
- Arduino IDE with ESP32 board support
- KiCad (for hardware modifications)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
