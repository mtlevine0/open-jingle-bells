# Wireless sync: making many players play in unison

Research notes on making any number of players strike the same songs together,
more reliably than the current ESP-NOW stream. Written 2026-10-21 against
`firmware/transmitter/transmitter.ino`, `firmware/player/player.ino` and the
esp32 Arduino core 3.3.11 (ESP-IDF 5.5). Nothing here is built yet. None of the
numbers below come from measurements on our own hardware.

> **Decision (2026-10-21):** we're building sync over **UDP on the LAN**,
> not ESP-NOW. This document's "broadcast the schedule, not the notes" idea
> carries over. The ESP-NOW-specific parts (beacons as the clock, relaying,
> channel scanning, LR rates) don't. The design is in
> [minstrel-firmware-plan.md](minstrel-firmware-plan.md).

## How it works today

The transmitter sends one ESP-NOW broadcast per frame, every 125–333 ms
depending on the song. The packet holds a single `uint16_t` ROM word. Each
player strikes as soon as the packet arrives.

This has several weaknesses:

1. **A lost packet is a lost note.** Broadcast frames get no ACK and no MAC
   retry. The packets carry no sequence number either, so the losses can't be
   counted.
2. **The radio is the clock.** Notes play whenever a packet happens to arrive.
   A player has no idea where it is in the song.
3. **The player may cause some of its own losses.** `OnDataRecv` calls
   `Serial.printf`, and that callback runs in the Wi-Fi task. On the S3's USB
   CDC, a write can block while the host isn't draining the port, which stalls
   the Wi-Fi task. This is unconfirmed but cheap to rule out.
4. **The radio runs on defaults.** Power save isn't turned off, and TX power and
   broadcast rate are never set. The transmitter sets `WIFI_PROTOCOL_LR` only,
   so nobody has checked which rate the broadcasts actually go out at.
5. **One antenna limits everything.** Range depends entirely on the
   transmitter's antenna, and the transmitter is a single point of failure.
6. **Timing drifts slightly.** `delay(STEP_MS)` follows the send and the
   prints, so the frame period runs a little long. This is minor.

## Core idea: broadcast the schedule, not the notes

Songs are tiny. The largest, `get_lucky_rom.h`, has 1,737 frames, which is
about 3.5 KB as `uint16_t`. The whole library plus the original ROM dump comes
to well under 100 KB, and the WROOM-1 N8 has 8 MB of flash. So every player
can carry every song.

### The beacon

A leader broadcasts a small beacon several times a second (4–10 Hz). Each
beacon holds the **complete** current state, so any one beacon on its own is
enough to sync a player:

```c
struct Beacon {             // ~32 bytes
  uint16_t magic, version;
  uint8_t  group;           // separates two choirs on the same channel
  uint8_t  leaderPrio;      // leader election
  uint8_t  hops;            // relay depth (see "Range: relaying")
  uint32_t seq;             // loss counting and dedupe
  uint32_t libraryCrc;      // players with a different song table stay silent
  uint8_t  state;           // stopped / playing
  uint8_t  songId;
  int64_t  songStartUs;     // leader time when frame 0 plays
  int64_t  leaderNowUs;     // leader time stamped just before esp_now_send
};
```

The leader schedules state changes about 0.5 s in the future: "song 7 starts
at T". It repeats that start time in every beacon until the song ends.
Because the beacons are idempotent, a player only needs one of them to
arrive before T.

### Player clock

- On receive, compute `offset = leaderNowUs - esp_timer_get_time()`. Network
  delay only ever adds to the true offset, so filter with a running minimum
  over the last few seconds, or with an EWMA that rejects outliers.
- To play, compute `frame = (esp_timer_get_time() + offset - songStartUs) /
  stepUs`. When the index moves forward, strike that frame once. If a
  correction jumps the index ahead, strike only the newest frame and skip the
  older ones.
- During playback, apply offset corrections gradually (slew them) rather than
  stepping. That way a correction never doubles or drops a frame.
- Use `esp_timer_get_time()` (64-bit µs), not `millis()`.

### Why this is robust and accurate

- **Losses barely matter.** Between beacons a player runs on its own crystal.
  Assuming the usual ±10 ppm module spec (not measured), two players drift
  apart by at most about 1.2 ms per minute. So a gap of 10 s or more without
  a beacon is inaudible. A player that joins late jumps straight into the
  middle of the song.
- **Leader jitter cancels.** Every receiver hears the same broadcast at the
  same instant. Whatever delay the leader added between stamping
  `leaderNowUs` and putting the packet on air is the same for all of them.
  Players should agree with each other to well under 1 ms. If the leader also
  plays bells, it can offset its own playback by a measured TX latency.
- **The target is loose.** Sound covers 3.4 m in 10 ms, so the spacing
  between units already exceeds the sync error. A few milliseconds is plenty.
- **Per-device strike latency can be calibrated.** Driver and solenoid delay
  may differ between units. `bell_listen.py` could measure each one, and the
  player could store a per-unit offset in NVS.
- **Refinement, only if needed:** `esp_now_recv_info_t.rx_ctrl->timestamp` is
  the MAC receive time in µs, which is more precise than a timestamp taken in
  the callback. However, its timebase is the Wi-Fi MAC's, not `esp_timer`'s,
  so we would need to test it before relying on it.

### Leaderless operation

Any player can lead. It takes the lead from a serial `play <n>` command or the
PLAY button (IO42 on the player board). That makes the dedicated transmitter
optional: it becomes a player with no bells.

- The leader is the highest `leaderPrio` heard, with ties broken by lowest
  MAC.
- If the leader goes silent, players keep playing the current song to the end
  from their own clocks. The next leader adopts the same timeline (same
  `songStartUs` in its own estimate of leader time), so the handover causes no
  hiccup.

### Range: relaying

Players can optionally rebroadcast each new beacon (by `seq`) after a random
0–20 ms delay. A relayed beacon carries `hops + 1`, and its `leaderNowUs` is
restamped with the relayer's estimate of leader time. Players prefer the
lowest hop count they hear. Each hop adds roughly 0.1–1 ms of error. Beacons
are small and infrequent, so flooding them costs very little, and once units
relay, the leader's antenna stops being the limit.

### Getting songs onto players

1. **Compiled in (phase 1).** A script turns the `*_rom.h` headers into a
   `songs.h` table of `{name, stepMs, frames, count}` entries. It takes
   `stepMs` from each header's `One 12-bit frame every N ms` line. The CRC of
   this table goes in `libraryCrc`, so a player with a stale build stays
   silent instead of playing the wrong notes.
2. **Pushed over the air (phase 2).** The installed SDK supports ESP-NOW v2
   (`ESP_NOW_MAX_DATA_LEN_V2` = 1470 bytes), so a whole song fits in 3
   packets. The leader repeats the song chunks during the lead-in. Players
   check each chunk against a CRC and keep the song in RAM or LittleFS. With
   this, a new song needs no reflash. (v2 needs every peer on a v2-capable
   IDF; all of ours are.)

### Fallback: streaming ahead

Some sources aren't in the library, such as a physical cartridge read by the
player board or frames sent over USB serial. For those, keep a streaming mode,
but send the frames ahead of time. Each packet carries `{firstFrame, playAtUs,
next ~16 frames}`, so every frame shows up in about 16 consecutive packets.
Players dedupe by frame index and play on the shared clock. The cost is a
lead time of about 16 frames.

## Radio hygiene (useful in any design)

All of these APIs are in the core 3.3.11 headers:

- Call `esp_wifi_set_ps(WIFI_PS_NONE)` on players.
- Call `esp_wifi_set_max_tx_power(80)`, which is about 20 dBm.
- Set the broadcast peer's rate explicitly with `esp_now_set_peer_rate_config()`.
  `WIFI_PHY_RATE_LORA_250K` gives the most range but needs LR on every
  receiver. `WIFI_PHY_RATE_1M_L` works with non-LR receivers. Measure both.
- Don't print or block in `OnDataRecv`. Copy the packet and the receive
  timestamp into a queue and return.
- Choose a quiet channel. Players could also scan channels 1–13 for beacons at
  boot, so the channel can change without a reflash.
- Keep the PCB antenna clear of the bells and any metal enclosure. Choir rev 2
  already puts the module antenna at the top edge.
- Add counters to `status`: packets received, packets lost (from `seq` gaps),
  and last/min RSSI (`rx_ctrl->rssi`). These turn "notes are missed" into a
  loss rate per device, and separate RF trouble (low RSSI) from software
  trouble (good RSSI but gaps).

## LAN instead of ESP-NOW

LAN is good for control but not for sync.

**For:**

- **A free precise clock.** Stations on the same AP share its TSF timer
  through `esp_wifi_get_tsf_time()`, which is µs-level common time with no
  sync code. This doesn't hold across mesh or multi-AP networks, where each AP
  has its own TSF. SNTP is the fallback and gives a few ms.
- **Practical extras.** A web UI on a phone, OTA firmware updates, and song
  upload over HTTP.
- **Reliable commands.** Unicast commands get acknowledged and retried.

**Against:**

- Wi-Fi broadcast and multicast go out at a basic rate with no ACK, so they
  are no more reliable than ESP-NOW broadcast. Some consumer and mesh APs also
  drop, delay or unicast-convert multicast.
- With power save on, broadcasts are buffered until the DTIM beacon. That can
  add hundreds of ms.
- It needs venue Wi-Fi and credential provisioning, which is awkward for an
  outdoor display. Consumer APs also cap how many clients can connect.
- Once a unit associates with an AP, its ESP-NOW is pinned to the AP's
  channel.

With scheduled starts, the transport hardly matters, because "play song 7 at
T" arrives early whichever way it travels. So the beacon protocol should be
independent of the transport. Later, one unit could join Wi-Fi to serve a web
UI and pass commands onto ESP-NOW for the rest.

## Plan

1. **Phase 0: measure.** Add `seq` to the current packets, apply the hygiene
   fixes, and add the loss and RSSI counters. Then measure loss per device with
   the existing transmitter.
2. **Phase 1: beacon sync.** Generate `songs.h`, add the beacon protocol and
   clock to `player.ino`, and let any player lead. The transmitter becomes
   optional.
3. **Phase 2: range and distribution.** Add beacon relaying, song push over
   ESP-NOW v2, and the streaming-ahead fallback for cartridge sources.
4. **Optional: Wi-Fi bridge.** One unit on Wi-Fi provides a web UI and OTA,
   and passes commands onto ESP-NOW.
