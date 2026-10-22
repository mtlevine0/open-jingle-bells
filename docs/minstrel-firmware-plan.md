# Minstrel firmware plan

Plan for the bell-unit firmware in `firmware/`, started 2026-10-21.
Every unit carries the song library, joins the same Wi-Fi network and plays in
unison with the others. Sync runs over **UDP on the LAN**. The units agree on
a shared group clock and a scheduled song timeline. Any unit serves a phone
control page.

Background research is in [wireless-sync.md](wireless-sync.md).

**Transport decision (2026-10-21): UDP instead of ESP-NOW.** The earlier
version of this plan used ESP-NOW beacons. We switched to UDP for these
reasons:

- **Coverage.** Units already join Wi-Fi for the control page, so range
  becomes the router's and its mesh nodes' coverage, not one unit's antenna.
- **No channel juggling.** Every unit simply uses the network's channel.
- **Easier tooling.** A laptop can watch, send and test sync packets with
  ordinary UDP tools.

**What we give up:**

- **A router is required.** Every unit needs Wi-Fi credentials and a network
  to join. A unit without Wi-Fi plays alone.
- **Broadcast timing is unreliable.** Wi-Fi broadcast delivery is delayed and
  jittery (see "Clock"). So the clock comes from unicast exchanges, and state
  changes are scheduled ahead of time.

## Where we stand (2026-10-21)

**Summary.**

- **Milestones 1, 3 and 4.** Written and running on the rev 1 choir board: it
  joins Wi-Fi from the phone setup page and plays from the control page.
- **Milestone 2 (UDP sync).** Written and compiles, with no warnings, for both
  boards (firmware 0.2.0, about 1.02 MB of the 3.3 MB app slot). Running on
  hardware: boards sync and play together. The logic-analyzer skew checks
  are still open; milestone 5 (robustness) is next.

### Done

| Area | State | Where |
|------|-------|-------|
| Song table generator | Done. 26 module-801 songs from `roms.csv`, 8,349 frames, library CRC `49D40789` | `minstrel-build-songs` → `firmware/songs.h` |
| Strike engine | Done. A mutex-guarded bus write; an `esp_timer` ends the pulse | `bells.cpp` |
| Playback scheduler | Done. Its own task on core 1 at priority 5: sleep, then spin to the µs. Late-strike counter and worst lateness. Skips to the due frame if more than 50 ms behind (a mid-song join; not counted as late). Modes: once, repeat, play all (1.5 s gap). `playbackRetime()` and `playbackStopAt()` for sync | `playback.cpp` |
| Settings in NVS | Done. Name, Wi-Fi credentials, strike length, setup AP channel, sync group, strike offset | `config.cpp` |
| Wi-Fi | Done. Setup AP `Minstrel-XXXX` with captive DNS, 20 s join timeout, fallback AP that retries every 30 s only when no phone is connected, AP kept 30 s after joining, mDNS `<name>.local`, power save off, 19.5 dBm | `net.cpp` |
| Group clock | Written. Group time = local + offset; minimum-RTT filter over 16 samples; slews at 0.5 ms/s once synced, steps while converging or past 20 ms | `clock.cpp` |
| UDP sync | Written. HELLO/STATE broadcasts and unicast TIME on port 47800 through `AsyncUDP`; peer table; clock-master and conductor election; reconcile of the group timeline onto local playback; `drop` for testing | `sync.cpp` |
| Control page | Done. Tabs: Play (with a group sync line), Bells, Health (group card with a row per unit), Setup (Wi-Fi, name, strike length, AP channel, group, strike offset). Light/dark, works at phone width, ~16 KB | `web_ui.h` |
| JSON API | Done. `/api/status` has a `sync` block with peers. See "Endpoints" | `web.cpp` |
| USB serial | Done. Adds `sync` and `drop <pct>` | `console.cpp` |
| Button and LED | Done. S1 short press toggles play/stop for the group; a 5 s hold forgets Wi-Fi | `firmware.ino` |
| Laptop monitor | Written. Decodes HELLO/STATE; `--time` probes the clock master. Packet layouts checked against the firmware's structs (14/90/46/42 bytes) | `minstrel-sync-monitor` |

### Checked on hardware

- **Group sync (milestone 2) works.** Boards see each other, sync their
  clocks and play together (reported 2026-10-21). A TIME probe from a PC to
  the clock master gave a 3.0 ms best RTT with a 22–49 µs turnaround, with
  occasional spikes up to 565 ms that the minimum-RTT filter discards.
- Wi-Fi setup from a phone and playback from the control page work on the
  rev 1 choir board (firmware 0.1.0, reported 2026-10-21).
- **Wrong notes, now fixed.** The first flash played the wrong bells.
  - Cause: a stale build. The player-board variant (`-DPLAYER_BOARD`) was the
    last one compiled, and a bare `arduino-cli upload` flashes the last build.
  - Fix: flash with `arduino-cli compile -u -p <port> ...` in one step, and
    check that Health shows "Board: merrie-minstrel-choir".

### Not yet checked on hardware

- **The measured milestone 2 checks.** Skew under 2 ms on a logic
  analyzer, with `drop 80`, with a power-saving phone on the network, a
  mid-song join, and losing the clock master. See the checklist under
  Milestones.
- **The milestone 1 timing check:** late strikes stay at 0 while serial and
  the page are busy.
- **Wi-Fi failure paths,** the captive-portal pop-up on iOS and Android, and
  renaming while connected.

### How the code differs from the plan

- **Song list.** It comes from `tools/roms/roms.csv`, not a new
  `songs.txt`.
- **Scheduler.** It's in `playback.cpp`. `sync.cpp` maps the group timeline
  onto it.
- **Endpoints.**
  - Added `/api/next` and `/api/mode`.
  - `/api/strike` takes a 12-bit `word`.
  - Commands that sync can't take yet answer 409 with a message, for example
    while the clock is still syncing.
  - `/update` is deferred to milestone 5.
- **Play lead time.** A conductor with no peers uses a 150 ms lead instead of
  1 s, and stops at once.
- **HELLO** also carries the unit's `playing` flag and current song.
- **Library mismatch.** A unit whose library differs from the conductor's
  stops and shows the mismatch. If the mismatched unit itself conducts, the
  *others* see the mismatch and stay silent.

### Known gaps

- **Late join with the conductor gone.** A unit that joins while the
  conductor is gone doesn't hear STATE and stays idle. Conductor takeover
  (milestone 5) fixes this.
- **Sync packets aren't authenticated.**

## Decisions

- **Board.** The target is the **merrie-minstrel-choir** board (rev 1): bell
  GPIOs as in `board.h`'s choir map, LED on IO35, S1 (GPIO0/BOOT) as the
  user button (active low after boot), native USB. The player board stays a
  build flag (`PLAYER_BOARD`) but isn't tested in this plan.
- **One firmware on every unit.** Every unit joins the same Wi-Fi network and
  serves the control page. Play or Stop on any unit makes it the conductor.
- **Transport.** UDP on the LAN, using the core's `AsyncUDP`. Its receive
  callback runs in the network task, so timestamps don't depend on `loop()`.
  - Group-wide packets go to the subnet broadcast address. Broadcast reaches
    every station in the network without the IGMP-snooping problems
    multicast can have on consumer routers.
  - Clock exchanges are unicast.
- **Web stack.** The built-in `WebServer` with a single embedded HTML page that
  polls `GET /api/status` about once a second. `DNSServer`, `ESPmDNS`,
  `Preferences`, `HTTPUpdateServer` and `AsyncUDP` all come with esp32 core
  3.3.11. There are no third-party libraries.
- **Songs are compiled in.** A generated `songs.h` holds them. Every unit must
  carry the same library, checked by CRC.
- **What it replaces.** The new sketch replaces `firmware/player/player.ino`
  and `firmware/transmitter/transmitter.ino`. Both were deleted on
  2026-10-21 once milestone 2 worked on hardware; the Python tools and ROM
  headers beside them stay.

## Sketch layout

Everything is in `firmware/`. Arduino compiles every `.cpp` file in a
sketch folder.

| File               | Contents | State |
|--------------------|----------|-------|
| `firmware.ino`     | `setup()` / `loop()`; calls each module's `begin()` / `service()`; button and LED | written |
| `board.h`          | Pin maps (choir default, `PLAYER_BOARD` option), board name, note names | written |
| `bells.cpp/.h`     | Strike engine: bus write, pulse end by `esp_timer`; later the per-unit strike offset | written |
| `songs.h`          | Generated song table (see below) | generated |
| `playback.cpp/.h`  | Scheduler task, play modes, song accessors | written |
| `config.cpp/.h`    | Settings stored in NVS through `Preferences` | written |
| `net.cpp/.h`       | Wi-Fi state machine: setup AP, captive portal, STA connect, mDNS | written |
| `web.cpp/.h`       | HTTP routes and JSON | written |
| `web_ui.h`         | The control page as a raw string literal | written |
| `console.cpp/.h`   | USB serial commands | written |
| `json.h`, `version.h` | JSON string escaping; firmware version string | written |
| `clock.cpp/.h`     | Group time: offset to the clock master, RTT filter, slew | written |
| `sync.cpp/.h`      | UDP socket, packets, elections, peer table; drives `playback` | written |

Build and flash in one step from `firmware/`:
`arduino-cli compile -u -p <port> -b esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=default_8MB`.
The default 8 MB scheme has two OTA app slots of about 3.3 MB each. Don't run
a bare `upload` after building the other board's variant: it flashes the last
build.

## Timing must not depend on `loop()`

`WebServer::handleClient()`, DNS and Wi-Fi reconnects can each hold `loop()`
for tens of ms. So:

- **Scheduler task.** A FreeRTOS task pinned to core 1, at a priority above
  `loop()`. It sleeps until about 2 ms before a frame, spins to the exact µs,
  then strikes. Wi-Fi runs on core 0. (Built.)
- **Pulse end.** A one-shot `esp_timer` ends each strike pulse. (Built.)
- **UDP receive.** The `AsyncUDP` `onPacket` callback stamps
  `esp_timer_get_time()` as soon as a packet arrives. It answers TIME_REQ right
  there, so the master's receive and send times are tight. Other packets are
  copied to a queue that `loop()` drains. The callback never prints or blocks.
- **Late-strike counter.** The scheduler counts strikes more than 2 ms late,
  and the Health page shows the count. (Built.)

## Song table

`minstrel-build-songs` reads a manifest CSV, by default
`tools/roms/roms.csv`. It uses the `song_title` and `file`
columns, keeps the row order as the song ids, and checks each song's frame
count against the `frames` column. The script writes
`firmware/songs.h`:

```c
struct Song { const char *name; uint16_t stepMs; uint16_t count; const uint16_t *frames; };
static const uint16_t SONG_0_FRAMES[] = { ... };  // const, so it stays in flash
static const Song SONGS[] = { {"Test Scale", 200, 109, SONG_0_FRAMES}, ... };
static const uint32_t LIBRARY_CRC = 0x...;        // over names, steps and frames
```

`stepMs` comes from each header's `One 12-bit frame every N ms` line. The
first library is the 26 module-801 songs split out of the ROM dump: 8,349
frames, about 17 KB. Only `playback.cpp` includes `songs.h`; the other modules
use its accessor functions.

## Roles

A group has two roles. They are kept separate so that changing who's in charge
never disturbs the clock.

- **Clock master.** The unit whose clock defines *group time*. It is the
  lowest MAC among units heard in the last 6 s, including itself. It changes
  only when that unit leaves or a lower MAC joins.
- **Conductor.** The unit that issued the latest command (Play, Stop, Next,
  mode). It broadcasts STATE. It changes whenever someone presses a control on
  any unit's page, its button, or its serial console.

Every unit keeps `groupNow() = esp_timer_get_time() + offset`. The clock
master's offset is frozen at the moment it becomes master: 0 if it booted
alone, or its last synced offset if it took over from a master that left. So
group time stays continuous when the master changes.

## Sync protocol (UDP)

- **Port and address.** Port 47800. Group-wide packets go to the subnet
  broadcast address (for example 192.168.1.255). TIME packets are unicast.
- **Header.** Every packet starts with `{magic "MNST", version, type, group,
  srcMac[6]}`. `group` comes from config, so two choirs can share a network.
  Packets from other groups or versions are ignored.

**HELLO**, broadcast by every unit every 2 s with ±200 ms of random jitter.
Peers learn each other's IP addresses from these packets. Fields:

- name, firmware version, `libraryCrc`, uptime
- conductor epoch seen, whether its clock is synced, RTT to the master and
  offset jitter
- late strikes and Wi-Fi RSSI

**STATE**, broadcast by the conductor at 4 Hz and three times in quick
succession on every change:

```c
uint32_t epoch;          // conductor term; the highest wins, ties go to the lowest MAC
uint32_t seq;
uint32_t libraryCrc;
uint8_t  state;          // STOPPED, PLAYING
uint8_t  song;
uint8_t  mode;           // SINGLE, LOOP_SONG, PLAY_ALL
int64_t  startGroupUs;   // group time of frame 0 of `song`
int64_t  stopGroupUs;    // group time the stop takes effect (STOPPED only)
```

- **Scheduled changes.** A change is scheduled **1 s ahead** in group time:
  Play starts at `groupNow() + 1 s`, and Stop takes effect at
  `groupNow() + 300 ms`. Broadcast delivery can lag by a DTIM interval, so
  the lead time absorbs that lag. Every unit then acts at the same instant.
- **Idempotent.** STATE is repeated and idempotent, so any one copy is enough.
- **Joining mid-song.** A unit that joins in the middle of a song starts at
  the frame that's due; playback's skip logic already does this.
- **Play-all needs no STATE.** In play-all, each unit advances to the next
  song by itself. Every unit has the same library and timeline, so they agree.
  The conductor's STATE then names the new song with the same start time.

**TIME_REQ / TIME_RESP**, unicast between a unit and the clock master:

```
unit   --TIME_REQ {t1}-------------->  master   (t1: unit's group time at send)
unit   <--TIME_RESP {t1, t2, t3}-----  master   (t2: master rx, t3: master tx)
unit stamps t4 at receive
rtt    = (t4 - t1) - (t3 - t2)
offset correction = ((t2 - t1) + (t3 - t4)) / 2
```

### Clock

- **Rate.** Each unit sends TIME_REQ at 1 Hz. When it joins or the master
  changes, it sends a burst of 8 at 100 ms intervals instead.
- **Filter.** Keep the last 16 samples. Use the offset from the
  **minimum-RTT** sample, and discard samples whose RTT is more than 3× that
  minimum. On a LAN, the fastest exchanges are the most symmetric, so they
  give the truest offset.
- **Synced.** A unit counts as synced after 4 good samples. An unsynced unit
  doesn't play. Its page shows "syncing".
- **Corrections.**
  - If the offset changes by more than 20 ms, apply it at once. Playback
    restarts at the due frame.
  - Smaller changes are slewed through `playbackRetime()` at up to 0.5 ms per
    second, so a frame is never doubled or skipped.
- **Expected accuracy.** Wi-Fi unicast RTT is typically 2–10 ms. With
  minimum-RTT filtering, the offset error is bounded by path asymmetry and
  should be about 1 ms on one AP. It will be a few ms across mesh backhaul.
  Sound covers 3.4 m in 10 ms, so that's within the target.
- **Possible refinement.** Stations on the same AP share its TSF timer
  (`esp_wifi_get_tsf_time()`), which agrees to the µs. If both a unit and the
  master report the same BSSID, the TSF could replace the ping estimate. It
  breaks across mesh nodes and on roaming, so it would be an optimization,
  not the base.

### Conductor election

- **Taking over.** A command on any unit sets
  `epoch = highest epoch seen + 1`, and that unit starts sending STATE.
- **Stepping down.** A conductor that hears STATE with a higher epoch (or the
  same epoch from a lower MAC) stops sending and follows.
- **Conductor goes silent.** If no STATE arrives for 3 s, units keep playing
  their timeline. In repeat and play-all modes, the lowest-MAC synced unit
  takes over as conductor with the same timeline and `epoch + 1`, so playback
  carries on. In once mode, the song simply ends. (Milestone 5; until then a
  conductorless group stops after the current song.)
- **Library mismatch.** A unit whose `libraryCrc` differs from the
  conductor's doesn't play. Its page and LED show "library mismatch: reflash".

## Wi-Fi

The state machine in `net.cpp` is built:

1. **Unconfigured,** meaning NVS holds no credentials. Start the open AP
   `Minstrel-XXXX` with a captive portal at 192.168.4.1. The setup page scans
   networks and saves the credentials to NVS.
2. **Connecting.** STA only, with a 20 s timeout. On failure, the setup AP
   comes back and the unit retries every 30 s while no phone is on the AP.
3. **Connected.** The AP turns off 30 s after joining, or at once if no phone
   is on it. mDNS answers as `<name>.local`. Sync starts.
4. **Reset.** Holding S1 for 5 s after boot, `wifi forget`, or Forget on the
   page clears the credentials and returns to state 1.

**Radio settings.** Power save is off (`WiFi.setSleep(false)`) and TX power is
19.5 dBm. Power save off matters for sync: a dozing station receives unicast
late and broadcast only at DTIM.

## Control page and API

**Page.** A single page that works at phone width. Built, single-unit:

- **Now playing:** progress, Play/Stop/Next, mode, song list.
- **Bell test:** 12 buttons.
- **Health:** playback, network, device.
- **Setup:** Wi-Fi scan/join/forget, name, strike length, AP channel.

Milestone 2 added:

- **Now playing:** units in sync and the conductor's name.
- **Group:** one row per unit from HELLOs: name, link to its page, synced or
  syncing, RTT, offset jitter, late strikes, RSSI, library match.
- **Settings:** group number and strike offset.

**Endpoints:**

| Method | Path              | Purpose | Built |
|--------|-------------------|---------|-------|
| GET    | `/`               | Control page (opens on Setup when not on Wi-Fi) | yes |
| GET    | `/api/status`     | This unit, Wi-Fi, playback, sync state with peers, library | yes |
| GET    | `/api/songs`      | Song list with names, lengths and steps | yes |
| POST   | `/api/play`       | `song`, `mode`; this unit becomes conductor | yes |
| POST   | `/api/stop`       | This unit becomes conductor and stops the group | yes |
| POST   | `/api/next`       | Play the next song | yes |
| POST   | `/api/mode`       | `single`, `loop` or `all` | yes |
| POST   | `/api/strike`     | `word`; local bell test, never sent to the group | yes |
| GET/POST | `/api/config`   | Name, strike length, setup AP channel, `group`, `strike_offset_ms` | yes |
| GET    | `/api/wifi/scan`  | Visible networks (setup page) | yes |
| POST   | `/api/wifi`       | `ssid`, `pass`; save and reconnect | yes |
| POST   | `/api/wifi/forget`| Clear credentials, back to setup AP | yes |
| GET/POST | `/update`       | Firmware upload via `HTTPUpdateServer` | milestone 5 |

POST bodies are form-encoded (`application/x-www-form-urlencoded`).

**Security.** None by default, because the units are on a home LAN. An optional
admin password (milestone 5) protects `/api/config`, `/api/wifi*` and
`/update` with HTTP Basic auth. Sync packets are not authenticated. Anyone on
the LAN who knows the format can send STATE.

**USB serial.** Built: `strike`, `note`, `pulse`, `notes`, `songs`, `play`,
`stop`, `next`, `mode`, `wifi`, `name`, `status`, `help`, plus the milestone 2
commands `sync` and `drop <pct>`:

- `sync` shows the role, clock master, conductor, offset, RTT and peers.
- `drop <pct>` discards that percentage of received sync packets, for testing.

## Milestones

Each milestone ends with a check on real choir boards.

1. **Local playback.** *Status: built and running; the timing check is
   still open.* Sketch skeleton, `board.h`, the bells engine with the
   scheduler task, `build_songs.py` and `songs.h`, serial `play`/`stop`/`songs`.
   *Check:* one board plays a full song from serial. The late-strike counter
   reads 0 while serial and the page are busy.
2. **UDP sync.** *Status: running on hardware; group playback works
   (reported 2026-10-21). The measured checks below are still open.* Add `clock` and `sync`,
   `playbackRetime()`, the `group` and strike-offset settings, the `sync` and
   `drop` commands, and group health on the page.
   *Check (2–3 boards on one network):*
   - Measure bell-line skew between boards with a logic analyzer or scope on
     the same bell GPIO. Target under 2 ms.
   - Repeat with a phone connected to the network in power save, so
     broadcasts are DTIM-delayed.
   - With `drop 80`, the skew stays under 2 ms.
   - A board powered up mid-song joins and plays in time.
   - Play on board B while A conducts: B takes over cleanly.
   - Power off the clock master mid-song: the others carry on in time.
3. **Wi-Fi.** *Status: built and joins from a phone.*
   *Check:* the failure paths listed under "Not yet checked on hardware".
4. **Control page.** *Status: built, including the group card (not yet seen
   with real peers).*
   *Check:* full control of the group from a phone through any unit, and the
   group table shows every unit.
5. **Robustness.** *Status: not started.*
   - Conductor-silent takeover in repeat and play-all.
   - OTA through `/update`.
   - Admin password.
   - ~~Delete `player.ino` and `transmitter.ino`, and update
     `firmware/README.md`.~~ Done 2026-10-21.
   - *Check:* an overnight play-all loop on all units with the conductor and
     the clock master power-cycled several times.

Later, out of scope here:

- **Setting up many units.** Putting each unit on Wi-Fi one at a time is
  tedious. Options: the serial `wifi` command (exists), or a configured unit
  sharing credentials with new ones.
- **Playing without a router.** One unit could host a fixed-name AP for the
  others to join.
- **Song push.** Sending songs over HTTP from the conductor, instead of
  reflashing.

## Risks and open questions

- **Client isolation.** Guest networks and some routers block traffic between
  stations. With it on, units can't see each other, and the page would show
  an empty group. Units must be on a normal (non-guest) network.
- **Mesh networks.** Broadcast crosses mesh backhaul, but with extra delay
  and jitter. The 1 s lead absorbs this for STATE, and minimum-RTT filtering
  handles it for the clock. Accuracy across nodes still needs measuring.
- **Broadcast loss.** Broadcasts get no ACK or retry on Wi-Fi.
  - STATE at 4 Hz, plus the burst on each change, covers this; any one copy
    is enough.
  - HELLO loss only delays the group table.
- **Router quirks.** Some APs convert broadcast to unicast, or throttle it.
  Either way STATE still arrives, but the latency changes; the 1 s lead covers
  that.
- **Phones on a captive portal with no internet.** iOS shows the sheet fine.
  Some Android versions may route traffic over cellular, in which case
  browsing to 192.168.4.1 still works.
- **Choir board:**
  - S1 is the BOOT strap, so S1 must not be held at reset unless you want
    download mode. The 5 s hold therefore starts after boot.
  - IO35 for the LED requires the N8 module (no octal PSRAM).
