#!/usr/bin/env python3
"""Watch minstrel group sync on the LAN.

Listens on the sync port (UDP 47800) and decodes the HELLO and STATE
broadcasts that units send (layouts in firmware/sync.cpp). TIME
packets are unicast between units, so they don't show up here.

With --time IP it sends TIME requests to that unit, as a unit would, and
prints the round trip. Only the clock master answers. Run one mode at a time:
both bind the sync port.

    minstrel-sync-monitor
    minstrel-sync-monitor --group 2 --summary
    minstrel-sync-monitor --time 192.168.1.42
"""

import argparse
import os
import socket
import struct
import sys
import time

PORT = 47800
MAGIC = 0x54534E4D
PROTOCOL = 1
HELLO, STATE, TIME_REQ, TIME_RESP = 1, 2, 3, 4

HEADER = struct.Struct("<IBBBB6s")
HELLO_BODY = struct.Struct("<24s24sIIIBBBbiiI")
STATE_BODY = struct.Struct("<IIIBBBBqq")
TIME_BODY = struct.Struct("<Iqqq")
MODES = {0: "once", 1: "repeat", 2: "play-all"}


def mac_str(raw):
    return ":".join(f"{b:02x}" for b in raw)


def cstr(raw):
    return raw.split(b"\0", 1)[0].decode(errors="replace")


def us(value):
    return "-" if value < 0 else f"{value / 1000:.2f} ms"


def decode(data):
    if len(data) < HEADER.size:
        return None
    magic, version, kind, group, _, mac = HEADER.unpack_from(data)
    if magic != MAGIC or version != PROTOCOL:
        return None
    out = {"type": kind, "group": group, "mac": mac_str(mac)}
    body = data[HEADER.size:]
    if kind == HELLO and len(body) >= HELLO_BODY.size:
        (name, fw, crc, uptime, epoch, synced, playing, song, rssi, rtt, jitter,
         late) = HELLO_BODY.unpack_from(body)
        out.update(name=cstr(name), firmware=cstr(fw), crc=crc, uptime=uptime, epoch=epoch,
                   synced=bool(synced), playing=bool(playing), song=song, rssi=rssi,
                   rtt=rtt, jitter=jitter, late=late)
    elif kind == STATE and len(body) >= STATE_BODY.size:
        epoch, seq, crc, playing, song, mode, _, start, stop = STATE_BODY.unpack_from(body)
        out.update(epoch=epoch, seq=seq, crc=crc, playing=bool(playing), song=song,
                   mode=MODES.get(mode, str(mode)), start=start, stop=stop)
    elif kind in (TIME_REQ, TIME_RESP) and len(body) >= TIME_BODY.size:
        seq, t1, t2, t3 = TIME_BODY.unpack_from(body)
        out.update(seq=seq, t1=t1, t2=t2, t3=t3)
    else:
        return None
    return out


def listen_socket(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("", port))
    return sock


def monitor(args):
    sock = listen_socket(args.port)
    sock.settimeout(1.0)
    names = {}
    peers = {}
    last_state = {}
    last_summary = 0.0
    print(f"listening on UDP {args.port}" + (f", group {args.group}" if args.group else ""))
    while True:
        try:
            data, (ip, _) = sock.recvfrom(2048)
        except socket.timeout:
            data = None
        now = time.time()
        if data:
            pkt = decode(data)
            if pkt and (not args.group or pkt["group"] == args.group):
                stamp = time.strftime("%H:%M:%S")
                if pkt["type"] == HELLO:
                    names[pkt["mac"]] = pkt["name"]
                    peers[pkt["mac"]] = (now, ip, pkt)
                    if not args.summary:
                        print(f"{stamp} HELLO {pkt['name']:<20} {ip:<15} g{pkt['group']} "
                              f"{'synced ' if pkt['synced'] else 'syncing'} epoch {pkt['epoch']} "
                              f"rtt {us(pkt['rtt'])} jitter {us(pkt['jitter'])} late {pkt['late']} "
                              f"rssi {pkt['rssi']} lib {pkt['crc']:08X} "
                              f"{'playing ' + str(pkt['song']) if pkt['playing'] else 'idle'}")
                elif pkt["type"] == STATE:
                    key = (pkt["mac"], pkt["epoch"], pkt["seq"])
                    # STATE repeats at 4 Hz; print changes only.
                    if last_state.get(pkt["mac"]) != key:
                        last_state[pkt["mac"]] = key
                        who = names.get(pkt["mac"], pkt["mac"])
                        what = (f"PLAY song {pkt['song']} ({pkt['mode']}) at group {pkt['start'] / 1e6:.3f} s"
                                if pkt["playing"] else f"STOP at group {pkt['stop'] / 1e6:.3f} s")
                        print(f"{stamp} STATE from {who}: epoch {pkt['epoch']} seq {pkt['seq']} "
                              f"lib {pkt['crc']:08X} {what}")
        if args.summary and now - last_summary >= 2:
            last_summary = now
            live = {m: v for m, v in peers.items() if now - v[0] < 6}
            print(f"--- {time.strftime('%H:%M:%S')} {len(live)} units")
            for mac, (seen, ip, p) in sorted(live.items()):
                print(f"  {p['name']:<20} {ip:<15} {'synced ' if p['synced'] else 'syncing'} "
                      f"rtt {us(p['rtt']):>9} jitter {us(p['jitter']):>9} late {p['late']:<4} "
                      f"rssi {p['rssi']}")


def time_probe(args):
    # The master replies to the sync port, not the sender's port, so bind it
    # and skip the broadcasts that arrive there too.
    sock = listen_socket(args.port)
    sock.settimeout(0.2)
    mac = b"\x02" + os.urandom(5)  # locally administered, never a unit's
    seq = 0
    print(f"TIME requests to {args.time}:{args.port}, group {args.group or 1} (only the clock master answers)")
    while True:
        seq += 1
        t1 = time.monotonic_ns() // 1000
        req = HEADER.pack(MAGIC, PROTOCOL, TIME_REQ, args.group or 1, 0, mac) + TIME_BODY.pack(seq, t1, 0, 0)
        sock.sendto(req, (args.time, args.port))
        deadline = time.monotonic() + 1.0
        reply = None
        while reply is None and time.monotonic() < deadline:
            try:
                data, _ = sock.recvfrom(2048)
            except socket.timeout:
                continue
            t4 = time.monotonic_ns() // 1000
            pkt = decode(data)
            if pkt and pkt["type"] == TIME_RESP and pkt["t1"] == t1:
                reply = (pkt, t4)
        if reply:
            pkt, t4 = reply
            rtt = (t4 - t1) - (pkt["t3"] - pkt["t2"])
            print(f"seq {seq:4}  rtt {rtt / 1000:7.2f} ms  master turnaround {pkt['t3'] - pkt['t2']} us")
        else:
            print(f"seq {seq:4}  no reply (not the clock master, wrong group, or unreachable)")
        time.sleep(args.interval)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--group", type=int, default=0, help="only this group (default: all)")
    parser.add_argument("--summary", action="store_true", help="a unit table every 2 s instead of every HELLO")
    parser.add_argument("--time", metavar="IP", help="send TIME requests to this unit (the clock master)")
    parser.add_argument("--interval", type=float, default=1.0, help="seconds between TIME requests")
    args = parser.parse_args()
    try:
        if args.time:
            time_probe(args)
        else:
            monitor(args)
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == "__main__":
    main()
