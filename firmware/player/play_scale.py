#!/usr/bin/env python3
"""Play the ROM intro scale on the bells through the player's USB serial port.

The scale is read from transmitter/rom_dump_hex.h, so the player strikes
exactly the words the working transmitter sends: the intro's twelve
single-bell frames from high to low, then the same bells from low to high.

Uses only the Python standard library (Linux/macOS termios). Every command
must be acknowledged with "OK" by the firmware, so a missing or wrong reply
fails the run.

    python firmware/player/play_scale.py --port /dev/ttyACM1
"""

import argparse
import os
import re
import select
import sys
import termios
import time
from pathlib import Path

ROM = Path(__file__).resolve().parent.parent / "transmitter" / "rom_dump_hex.h"
STEP_MS = 200  # transmitter frame interval


def rom_scale(path):
    """Return the intro's run of twelve distinct single-bell words."""
    words = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)", path.read_text())]
    run = []
    for word in words:
        if word == 0:
            continue
        if word.bit_count() == 1 and word not in run:
            run.append(word)
            if len(run) == 12:
                return run
        else:
            run = []
    raise ValueError(f"no 12-bell scale found in {path}")


class Player:
    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0  # iflag
        attrs[1] = 0  # oflag
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0  # lflag
        attrs[4] = attrs[5] = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.buffer = b""

    def close(self):
        os.close(self.fd)

    def readline(self, timeout):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            ready, _, _ = select.select([self.fd], [], [], remaining)
            if ready:
                self.buffer += os.read(self.fd, 256)
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def drain(self):
        while self.readline(0.2) is not None:
            pass
        self.buffer = b""

    def command(self, text, timeout=1.0):
        os.write(self.fd, text.encode() + b"\n")
        while True:
            line = self.readline(timeout)
            if line is None:
                raise RuntimeError(f"no reply to {text!r}")
            if line.startswith("OK"):
                return line
            if line.startswith("ERR"):
                raise RuntimeError(f"{text!r} -> {line}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default="/dev/ttyACM1")
    parser.add_argument("--rom", type=Path, default=ROM)
    parser.add_argument("--step-ms", type=float, default=STEP_MS, help="time between notes")
    parser.add_argument("--down-only", action="store_true", help="play only the ROM order, high to low")
    args = parser.parse_args()

    scale = rom_scale(args.rom)
    words = scale if args.down_only else scale + scale[-2::-1]

    player = Player(args.port)
    try:
        player.drain()
        print(player.command("status"))
        for index, word in enumerate(words):
            reply = player.command(f"strike 0x{word:03X}")
            direction = "down" if index < len(scale) else "up"
            print(f"{direction:>4} bell {scale.index(word) + 1:2}  {reply}")
            time.sleep(args.step_ms / 1000)
        print(f"PASS: {len(words)} strikes acknowledged")
    except RuntimeError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    finally:
        player.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
