#include "sync.h"
#include "config.h"
#include "library.h"
#include "version.h"
#include <AsyncUDP.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_timer.h>

static const uint16_t PORT = 47800;
static const uint32_t MAGIC = 0x54534E4D;  // "MNST" on the wire
static const uint8_t PROTOCOL = 2;  // 2: STATE names composed songs

static const uint32_t HELLO_MS = 2000;
static const uint32_t HELLO_JITTER_MS = 200;
static const uint32_t PEER_TIMEOUT_MS = 6000;
static const uint32_t LISTEN_MS = 3000;           // before claiming master alone
static const uint32_t STATE_MS = 250;
static const uint32_t STATE_BURST_MS = 50;
static const uint32_t CONDUCTOR_TIMEOUT_MS = 3000;
static const uint32_t TIME_MS = 1000;
static const uint32_t TIME_BURST_MS = 100;
static const int TIME_BURST = 8;
static const uint8_t SYNCED_SAMPLES = 4;
static const uint32_t RECONCILE_MS = 250;
static const int64_t LEAD_GROUP_US = 1000000;     // play, with peers
static const int64_t LEAD_ALONE_US = 150000;      // play, no peers
static const int64_t STOP_LEAD_US = 300000;       // stop, with peers
static const int64_t TIMELINE_TOLERANCE_US = 3000;
static const int64_t NEW_TIMELINE_US = 500000;      // a start moved this far is a new song
static const int MAX_PEERS = 16;

enum PacketType : uint8_t { PKT_HELLO = 1, PKT_STATE = 2, PKT_TIME_REQ = 3, PKT_TIME_RESP = 4 };

struct __attribute__((packed)) Header {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint8_t group;
  uint8_t reserved;
  uint8_t mac[6];
};

struct __attribute__((packed)) HelloPacket {
  Header h;
  char name[24];
  char firmware[24];
  uint32_t libraryCrc;
  uint32_t uptimeS;
  uint32_t epoch;
  uint8_t synced;
  uint8_t playing;
  uint8_t song;
  int8_t rssi;
  int32_t rttUs;
  int32_t jitterUs;
  uint32_t lateStrikes;
};

struct __attribute__((packed)) StatePacket {
  Header h;
  uint32_t epoch;
  uint32_t seq;
  uint32_t libraryCrc;
  uint8_t playing;
  uint8_t song;
  uint8_t mode;
  uint8_t reserved;
  int64_t startGroupUs;
  int64_t stopGroupUs;
  uint32_t songId;      // composed song, or 0 for compiled-in song number `song`
  uint32_t songCrc;     // composed song's contents
  uint16_t firstFrame;  // where the first song starts (play from a step)
  uint16_t reserved2;
};
static_assert(sizeof(StatePacket) == 58, "ojb-sync-monitor decodes 58-byte STATE");

struct __attribute__((packed)) TimePacket {
  Header h;
  uint32_t seq;
  int64_t t1;  // requester's local send time
  int64_t t2;  // master's group time at receive
  int64_t t3;  // master's group time at send
};

// HELLO and STATE are handled in loop(); the UDP task queues them.
struct RxItem {
  uint8_t data[sizeof(HelloPacket)];
  uint8_t len;
  uint32_t ip;
};

struct Peer {
  uint64_t mac;
  uint32_t ip;
  uint32_t lastSeenMs;
  HelloPacket hello;
};

// The group's song timeline, as the conductor last described it.
struct GroupState {
  bool valid;
  uint32_t epoch;
  uint64_t conductor;
  uint32_t seq;
  bool playing;
  uint8_t song;         // conducting: our song number; following: the conductor's
  uint32_t songId;      // following: the conductor's composed song, or 0
  uint32_t songCrc;
  uint16_t firstFrame;
  PlayMode mode;
  int64_t startGroupUs;
  int64_t stopGroupUs;
  uint32_t heardMs;
  uint32_t conductorIp;
};

static AsyncUDP udp;
static QueueHandle_t rxQueue;
static uint8_t myMacBytes[6];
static uint64_t myMac;

// Shared with the UDP task.
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool isMaster = false;
static uint64_t masterMac = 0;
static volatile uint8_t dropPercent = 0;
static volatile uint32_t rxPackets = 0;
static volatile uint32_t droppedPackets = 0;

// loop() only.
static bool active = false;
static uint32_t activeSinceMs = 0;
static bool synced = false;
static Peer peers[MAX_PEERS];
static int peerCount = 0;
static GroupState gs = {};
static bool conducting = false;
static bool libraryMismatch = false;
static uint32_t maxEpoch = 0;
static uint32_t nextHelloMs = 0;
static uint32_t nextStateMs = 0;
static int stateBurst = 0;
static uint32_t nextTimeMs = 0;
static int timeBurst = 0;
static uint32_t timeSeq = 0;
static uint32_t nextReconcileMs = 0;

static uint64_t macToU64(const uint8_t *m) {
  uint64_t v = 0;
  for (int i = 0; i < 6; i++) {
    v = (v << 8) | m[i];
  }
  return v;
}

static void u64ToMac(uint64_t v, uint8_t *m) {
  for (int i = 5; i >= 0; i--) {
    m[i] = v & 0xFF;
    v >>= 8;
  }
}

static void fillHeader(Header *h, PacketType type) {
  h->magic = MAGIC;
  h->version = PROTOCOL;
  h->type = type;
  h->group = config.group;
  h->reserved = 0;
  memcpy(h->mac, myMacBytes, 6);
}

static void broadcast(const void *data, size_t len) {
  udp.broadcastTo((uint8_t *)data, len, PORT, TCPIP_ADAPTER_IF_STA);
}

// Group time <-> local playback time, including this unit's strike offset.
static int64_t localFromGroup(int64_t groupUs) {
  return clockGroupToLocal(groupUs) - (int64_t)config.strikeOffsetMs * 1000;
}

static int64_t groupFromLocal(int64_t localUs) {
  return clockLocalToGroup(localUs + (int64_t)config.strikeOffsetMs * 1000);
}

static uint64_t currentMaster() {
  portENTER_CRITICAL(&mux);
  uint64_t m = masterMac;
  portEXIT_CRITICAL(&mux);
  return m;
}

// --- UDP task ---------------------------------------------------------------

static void onPacket(AsyncUDPPacket &packet) {
  int64_t rxUs = esp_timer_get_time();
  size_t len = packet.length();
  if (len < sizeof(Header)) {
    return;
  }
  const uint8_t *data = packet.data();
  Header h;
  memcpy(&h, data, sizeof(h));
  if (h.magic != MAGIC || h.version != PROTOCOL || h.group != config.group) {
    return;
  }
  uint64_t src = macToU64(h.mac);
  if (src == myMac) {
    return;
  }
  if (dropPercent && esp_random() % 100 < dropPercent) {
    droppedPackets = droppedPackets + 1;
    return;
  }
  rxPackets = rxPackets + 1;

  if (h.type == PKT_TIME_REQ && len >= sizeof(TimePacket)) {
    if (!isMaster) {
      return;
    }
    TimePacket p;
    memcpy(&p, data, sizeof(p));
    fillHeader(&p.h, PKT_TIME_RESP);
    p.t2 = clockLocalToGroup(rxUs);
    p.t3 = clockGroupNow();
    udp.writeTo((uint8_t *)&p, sizeof(p), packet.remoteIP(), PORT, TCPIP_ADAPTER_IF_STA);
  } else if (h.type == PKT_TIME_RESP && len >= sizeof(TimePacket)) {
    if (src != currentMaster()) {
      return;
    }
    TimePacket p;
    memcpy(&p, data, sizeof(p));
    clockAddSample(p.t1, p.t2, p.t3, rxUs);
  } else if ((h.type == PKT_HELLO && len >= sizeof(HelloPacket)) ||
             (h.type == PKT_STATE && len >= sizeof(StatePacket))) {
    RxItem item;
    item.len = min(len, sizeof(item.data));
    memcpy(item.data, data, item.len);
    item.ip = (uint32_t)packet.remoteIP();
    xQueueSend(rxQueue, &item, 0);
  }
}

// --- Peers and roles ----------------------------------------------------------

static Peer *findPeer(uint64_t mac) {
  for (int i = 0; i < peerCount; i++) {
    if (peers[i].mac == mac) {
      return &peers[i];
    }
  }
  return NULL;
}

static void handleHello(const HelloPacket &p, uint32_t ip) {
  uint64_t mac = macToU64(p.h.mac);
  Peer *peer = findPeer(mac);
  if (peer == NULL) {
    if (peerCount == MAX_PEERS) {
      return;
    }
    peer = &peers[peerCount++];
    peer->mac = mac;
    Serial.printf("sync: peer %.24s joined\n", p.name);
  }
  peer->ip = ip;
  peer->lastSeenMs = millis();
  peer->hello = p;
  peer->hello.name[sizeof(p.name) - 1] = '\0';
  peer->hello.firmware[sizeof(p.firmware) - 1] = '\0';
  maxEpoch = max(maxEpoch, p.epoch);
}

static void expirePeers() {
  uint32_t now = millis();
  for (int i = 0; i < peerCount;) {
    if (now - peers[i].lastSeenMs > PEER_TIMEOUT_MS) {
      Serial.printf("sync: peer %s left\n", peers[i].hello.name);
      peers[i] = peers[--peerCount];
    } else {
      i++;
    }
  }
}

static void setMaster(uint64_t mac) {
  portENTER_CRITICAL(&mux);
  bool changed = masterMac != mac;
  masterMac = mac;
  isMaster = mac == myMac;
  portEXIT_CRITICAL(&mux);
  if (!changed) {
    return;
  }
  if (mac == myMac) {
    Serial.println("sync: this unit is the clock master");
  } else if (mac != 0) {
    Peer *p = findPeer(mac);
    Serial.printf("sync: clock master is %s\n", p ? p->hello.name : "?");
    clockResetSamples();
    timeBurst = TIME_BURST;
    nextTimeMs = millis();
  }
}

// The master is the lowest MAC among synced units. With none synced, the
// lowest MAC heard claims it after listening, so units booting together agree.
static void updateMaster() {
  uint64_t best = synced ? myMac : UINT64_MAX;
  uint64_t lowestAny = myMac;
  for (int i = 0; i < peerCount; i++) {
    lowestAny = min(lowestAny, peers[i].mac);
    if (peers[i].hello.synced && peers[i].mac < best) {
      best = peers[i].mac;
    }
  }
  if (best == UINT64_MAX) {
    if (millis() - activeSinceMs >= LISTEN_MS && lowestAny == myMac) {
      synced = true;
      best = myMac;
    } else {
      setMaster(0);
      return;
    }
  }
  setMaster(best);
}

// --- Timeline ---------------------------------------------------------------

// Where the timeline (song, start) stands at group time `now`: playback moves
// to the next song once the last frame is struck, after a SONG_GAP_US pause.
// Returns false if a single-mode song has ended.
static bool projectTimeline(uint8_t *song, int64_t *start, PlayMode mode, int64_t now) {
  for (int i = 0; i < 64; i++) {
    int64_t stepUs = (int64_t)songStepMs(*song) * 1000;
    int64_t lastDue = *start + (songFrameCount(*song) - 1) * stepUs;
    if (now <= lastDue) {
      return true;
    }
    if (mode == MODE_SINGLE) {
      return false;
    }
    *start += songFrameCount(*song) * stepUs + SONG_GAP_US;
    if (mode == MODE_PLAY_ALL) {
      *song = (*song + 1) % songCount();
    }
  }
  return true;
}

// The conductor's song as one of ours: -1 while a composed song is missing or
// out of date here.
static int localSong() {
  if (gs.songId == 0) {
    return gs.song < builtinSongCount() ? gs.song : 0;
  }
  int s = songFindUser(gs.songId);
  return s >= 0 && songCrc(s) == gs.songCrc ? s : -1;
}

// Bring local playback in line with the group timeline.
static void reconcile() {
  if (!gs.valid || conducting) {
    return;
  }
  PlaybackStatus pb = playbackStatus();
  if (libraryMismatch) {
    if (pb.playing) {
      playbackStop();
    }
    return;
  }
  if (!synced) {
    return;
  }
  if (pb.mode != gs.mode) {
    playbackSetMode(gs.mode);
  }
  if (gs.playing) {
    int resolved = localSong();
    if (resolved < 0) {
      // Fetch it from the conductor; the next reconcile after it lands joins in.
      if (pb.playing) {
        playbackStop();
      }
      songFetch(gs.conductorIp, gs.songId, gs.songCrc);
      return;
    }
    uint8_t song = resolved;
    int64_t start = gs.startGroupUs;
    if (!projectTimeline(&song, &start, gs.mode, clockGroupNow())) {
      return;  // the song ended; playback stopped on its own
    }
    int64_t localStart = localFromGroup(start);
    if (pb.playing && pb.song == song && llabs(pb.startUs - localStart) < TIMELINE_TOLERANCE_US) {
      return;
    }
    playbackPlay(song, localStart, start == gs.startGroupUs ? gs.firstFrame : 0);
  } else if (pb.playing) {
    int64_t localStop = localFromGroup(gs.stopGroupUs);
    if (pb.stopAtUs == INT64_MAX || llabs(pb.stopAtUs - localStop) > TIMELINE_TOLERANCE_US) {
      playbackStopAt(localStop);
    }
  }
}

static void handleState(const StatePacket &p, uint32_t ip) {
  uint64_t mac = macToU64(p.h.mac);
  maxEpoch = max(maxEpoch, p.epoch);
  bool newer = p.epoch > gs.epoch || !gs.valid ||
               (p.epoch == gs.epoch && (mac < gs.conductor || (mac == gs.conductor && p.seq >= gs.seq)));
  if (!newer) {
    return;
  }
  if (conducting && mac != myMac) {
    Peer *peer = findPeer(mac);
    Serial.printf("sync: %s took over conducting\n", peer ? peer->hello.name : "another unit");
    conducting = false;
  }
  gs.valid = true;
  gs.epoch = p.epoch;
  gs.conductor = mac;
  gs.seq = p.seq;
  gs.playing = p.playing;
  gs.song = p.song;
  gs.songId = p.songId;
  gs.songCrc = p.songCrc;
  gs.firstFrame = p.firstFrame;
  gs.conductorIp = ip;
  gs.mode = p.mode <= MODE_PLAY_ALL ? (PlayMode)p.mode : MODE_SINGLE;
  gs.startGroupUs = p.startGroupUs;
  gs.stopGroupUs = p.stopGroupUs;
  gs.heardMs = millis();
  libraryMismatch = p.libraryCrc != songLibraryCrc();
  reconcile();
}

// As conductor, keep gs in step with local playback (play-all advances, a
// single song ending) before describing it to the group.
static void refreshConductedState() {
  if (!gs.playing) {
    return;
  }
  PlaybackStatus pb = playbackStatus();
  if (pb.playing) {
    int64_t start = groupFromLocal(pb.startUs);
    if (pb.song != gs.song || llabs(start - gs.startGroupUs) > NEW_TIMELINE_US) {
      gs.seq++;
      gs.firstFrame = 0;  // a repeat or the next song starts from the top
    }
    gs.song = pb.song;
    gs.startGroupUs = start;
  } else {
    gs.playing = false;
    gs.stopGroupUs = clockGroupNow();
    gs.seq++;
  }
}

static void sendState() {
  refreshConductedState();
  StatePacket p = {};
  fillHeader(&p.h, PKT_STATE);
  p.epoch = gs.epoch;
  p.seq = gs.seq;
  p.libraryCrc = songLibraryCrc();
  p.playing = gs.playing;
  p.song = gs.song;
  p.mode = gs.mode;
  p.startGroupUs = gs.startGroupUs;
  p.stopGroupUs = gs.stopGroupUs;
  p.songId = songUserId(gs.song);
  p.songCrc = songCrc(gs.song);
  p.firstFrame = gs.firstFrame;
  broadcast(&p, sizeof(p));
}

static void sendHello() {
  HelloPacket p = {};
  fillHeader(&p.h, PKT_HELLO);
  strncpy(p.name, config.name.c_str(), sizeof(p.name) - 1);
  strncpy(p.firmware, FIRMWARE_VERSION, sizeof(p.firmware) - 1);
  PlaybackStatus pb = playbackStatus();
  ClockStats cs = clockStats();
  p.libraryCrc = songLibraryCrc();
  p.uptimeS = millis() / 1000;
  p.epoch = max(maxEpoch, gs.epoch);
  p.synced = synced;
  p.playing = pb.playing;
  p.song = pb.song;
  p.rssi = WiFi.RSSI();
  p.rttUs = isMaster ? 0 : cs.bestRttUs;
  p.jitterUs = isMaster ? 0 : cs.jitterUs;
  p.lateStrikes = pb.lateStrikes;
  broadcast(&p, sizeof(p));
}

static void sendTimeRequest() {
  Peer *master = findPeer(currentMaster());
  if (master == NULL) {
    return;
  }
  TimePacket p = {};
  fillHeader(&p.h, PKT_TIME_REQ);
  p.seq = ++timeSeq;
  p.t1 = esp_timer_get_time();
  udp.writeTo((uint8_t *)&p, sizeof(p), IPAddress(master->ip), PORT, TCPIP_ADAPTER_IF_STA);
}

// --- Commands ---------------------------------------------------------------

// Take the baton: a new conductor term, announced in a quick burst.
static const char *beginCommand() {
  if (!synced) {
    return "clock still syncing with the group; try again in a moment";
  }
  gs.valid = true;
  gs.epoch = max(maxEpoch, gs.epoch) + 1;
  maxEpoch = gs.epoch;
  gs.conductor = myMac;
  gs.seq++;
  gs.heardMs = millis();
  conducting = true;
  libraryMismatch = false;
  return NULL;
}

static void announce() {
  stateBurst = 3;
  nextStateMs = millis();
}

const char *syncPlay(int song, int32_t fromFrame) {
  if (song < 0) {
    song = playbackStatus().song;
  }
  if (song >= songCount()) {
    return "unknown song";
  }
  fromFrame = constrain(fromFrame, 0, max(0, songFrameCount(song) - 1));
  // Starting from a step: frame 0 falls due that much earlier.
  int64_t backUs = (int64_t)fromFrame * songStepMs(song) * 1000;
  if (!active) {
    playbackPlay(song, fromFrame ? esp_timer_get_time() + LEAD_ALONE_US - backUs : 0, fromFrame);
    return NULL;
  }
  const char *err = beginCommand();
  if (err) {
    return err;
  }
  gs.playing = true;
  gs.song = song;
  gs.firstFrame = fromFrame;
  gs.mode = playbackStatus().mode;
  gs.startGroupUs = clockGroupNow() + (peerCount ? LEAD_GROUP_US : LEAD_ALONE_US) - backUs;
  gs.stopGroupUs = 0;
  playbackPlay(song, localFromGroup(gs.startGroupUs), fromFrame);
  announce();
  return NULL;
}

const char *syncStop() {
  if (!active) {
    playbackStop();
    return NULL;
  }
  const char *err = beginCommand();
  if (err) {
    return err;
  }
  gs.playing = false;
  gs.stopGroupUs = clockGroupNow() + (peerCount ? STOP_LEAD_US : 0);
  playbackStopAt(localFromGroup(gs.stopGroupUs));
  announce();
  return NULL;
}

const char *syncNext() {
  return syncPlay((playbackStatus().song + 1) % songCount());
}

const char *syncToggle() {
  return playbackStatus().playing ? syncStop() : syncPlay(-1);
}

const char *syncSetMode(PlayMode mode) {
  if (!active) {
    playbackSetMode(mode);
    return NULL;
  }
  bool wasConducting = conducting;
  const char *err = beginCommand();
  if (err) {
    return err;
  }
  if (!wasConducting && gs.playing) {
    // Carry on with the timeline we were following.
    refreshConductedState();
  }
  gs.mode = mode;
  playbackSetMode(mode);
  announce();
  return NULL;
}

void syncSetStrikeOffset(int16_t ms) {
  int16_t old = config.strikeOffsetMs;
  config.strikeOffsetMs = ms;
  playbackRetime(-(int64_t)(ms - old) * 1000);
}

void syncSetDropPercent(uint8_t pct) {
  dropPercent = min<uint8_t>(pct, 100);
}

// --- Service ----------------------------------------------------------------

void syncBegin() {
  esp_read_mac(myMacBytes, ESP_MAC_WIFI_STA);
  myMac = macToU64(myMacBytes);
  rxQueue = xQueueCreate(16, sizeof(RxItem));
  udp.onPacket(onPacket);
  if (!udp.listen(PORT)) {
    Serial.println("sync: UDP listen failed");
  }
}

static void goInactive() {
  active = false;
  synced = false;
  conducting = false;
  libraryMismatch = false;
  peerCount = 0;
  gs.valid = false;
  setMaster(0);
}

void syncReset() {
  if (active) {
    goInactive();
    Serial.printf("sync: switched to group %u\n", config.group);
  }
}

void syncService() {
  bool online = WiFi.status() == WL_CONNECTED;
  if (!online) {
    if (active) {
      goInactive();
      Serial.println("sync: off the network, playing alone");
    }
    return;
  }
  uint32_t now = millis();
  if (!active) {
    active = true;
    activeSinceMs = now;
    synced = false;
    nextHelloMs = now;
    Serial.printf("sync: listening on UDP %u, group %u\n", PORT, config.group);
  }

  RxItem item;
  while (xQueueReceive(rxQueue, &item, 0) == pdTRUE) {
    Header h;
    memcpy(&h, item.data, sizeof(h));
    if (h.type == PKT_HELLO) {
      HelloPacket p;
      memcpy(&p, item.data, sizeof(p));
      handleHello(p, item.ip);
    } else if (h.type == PKT_STATE) {
      StatePacket p;
      memcpy(&p, item.data, sizeof(p));
      handleState(p, item.ip);
    }
  }
  expirePeers();
  updateMaster();

  if (!isMaster && currentMaster() != 0) {
    if ((int32_t)(now - nextTimeMs) >= 0) {
      sendTimeRequest();
      if (timeBurst > 0) {
        timeBurst--;
        nextTimeMs = now + TIME_BURST_MS;
      } else {
        nextTimeMs = now + TIME_MS;
      }
    }
    int64_t applied = clockService(!synced);
    if (applied) {
      playbackRetime(-applied);
    }
    ClockStats cs = clockStats();
    if (!synced && cs.good >= SYNCED_SAMPLES) {
      synced = true;
      Serial.printf("sync: clock synced, rtt %ld us\n", (long)cs.bestRttUs);
      nextHelloMs = now;
      nextReconcileMs = now;
    }
  }

  if (conducting) {
    if ((int32_t)(now - nextStateMs) >= 0) {
      sendState();
      if (stateBurst > 0) {
        stateBurst--;
        nextStateMs = now + STATE_BURST_MS;
      } else {
        nextStateMs = now + STATE_MS;
      }
    }
  } else if ((int32_t)(now - nextReconcileMs) >= 0) {
    // Covers a STATE that arrived before the clock synced, and a timeline
    // that drifted out of tolerance.
    reconcile();
    nextReconcileMs = now + RECONCILE_MS;
  }

  if ((int32_t)(now - nextHelloMs) >= 0) {
    sendHello();
    nextHelloMs = now + HELLO_MS - HELLO_JITTER_MS + esp_random() % (2 * HELLO_JITTER_MS);
  }
}

// --- Status -----------------------------------------------------------------

static String nameOf(uint64_t mac) {
  if (mac == 0) {
    return "";
  }
  if (mac == myMac) {
    return config.name;
  }
  Peer *p = findPeer(mac);
  return p ? String(p->hello.name) : String("?");
}

SyncStatus syncStatus() {
  SyncStatus st;
  st.active = active;
  st.synced = synced;
  st.isMaster = isMaster;
  st.isConductor = conducting;
  st.conductorKnown = gs.valid && (conducting || millis() - gs.heardMs < CONDUCTOR_TIMEOUT_MS);
  st.libraryMismatch = libraryMismatch;
  st.epoch = gs.epoch;
  st.masterName = nameOf(currentMaster());
  st.conductorName = st.conductorKnown ? nameOf(gs.conductor) : String("");
  st.clock = clockStats();
  st.rxPackets = rxPackets;
  st.droppedPackets = droppedPackets;
  st.dropPercent = dropPercent;
  st.peerCount = peerCount;
  return st;
}

int syncPeers(SyncPeer *out, int max) {
  uint64_t master = currentMaster();
  bool conductorKnown = gs.valid && (conducting || millis() - gs.heardMs < CONDUCTOR_TIMEOUT_MS);
  int n = 0;
  for (int i = 0; i < peerCount && n < max; i++, n++) {
    const Peer &p = peers[i];
    SyncPeer &o = out[n];
    o.name = p.hello.name;
    o.firmware = p.hello.firmware;
    o.ip = IPAddress(p.ip);
    u64ToMac(p.mac, o.mac);
    o.synced = p.hello.synced;
    o.isMaster = p.mac == master;
    o.isConductor = conductorKnown && p.mac == gs.conductor;
    o.libraryMatch = p.hello.libraryCrc == songLibraryCrc();
    o.playing = p.hello.playing;
    o.song = p.hello.song;
    o.rssi = p.hello.rssi;
    o.rttUs = p.hello.rttUs;
    o.jitterUs = p.hello.jitterUs;
    o.lateStrikes = p.hello.lateStrikes;
    o.uptimeS = p.hello.uptimeS;
    o.ageMs = millis() - p.lastSeenMs;
  }
  return n;
}
