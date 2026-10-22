#include "web.h"
#include "bells.h"
#include "board.h"
#include "composer_ui.h"
#include "config.h"
#include "json.h"
#include "library.h"
#include "net.h"
#include "playback.h"
#include "sync.h"
#include "version.h"
#include "web_ui.h"
#include <LittleFS.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <WiFi.h>
#include <esp_system.h>

static WebServer server(80);

// Wi-Fi changes run shortly after the reply, so it reaches the phone first.
enum PendingAction { PENDING_NONE, PENDING_CONNECT, PENDING_FORGET };
static PendingAction pending = PENDING_NONE;
static uint32_t pendingAt = 0;
static String pendingSsid, pendingPass;
static const uint32_t PENDING_DELAY_MS = 300;

static void sendJson(const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

static void sendOk() {
  sendJson("{\"ok\":true}");
}

static void sendError(int code, const char *message) {
  server.send(code, "application/json", String("{\"ok\":false,\"error\":") + jsonString(message) + "}");
}

static const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "usb";
    case ESP_RST_EXT: return "external";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "other";
  }
}

static String ipString(const IPAddress &ip) {
  return jsonString(ip.toString());
}

static const char *boolJson(bool b) {
  return b ? "true" : "false";
}

static String syncJson() {
  SyncStatus st = syncStatus();
  String j;
  j.reserve(1200);
  j += "{\"active\":" + String(boolJson(st.active));
  j += ",\"synced\":" + String(boolJson(st.synced));
  j += ",\"group\":" + String(config.group);
  j += ",\"is_master\":" + String(boolJson(st.isMaster));
  j += ",\"is_conductor\":" + String(boolJson(st.isConductor));
  j += ",\"master\":" + jsonString(st.masterName);
  j += ",\"conductor\":" + (st.conductorKnown ? jsonString(st.conductorName) : String("null"));
  j += ",\"epoch\":" + String(st.epoch);
  j += ",\"library_mismatch\":" + String(boolJson(st.libraryMismatch));
  j += ",\"offset_us\":" + String((long long)st.clock.offsetUs);
  j += ",\"error_us\":" + String((long long)st.clock.errorUs);
  j += ",\"rtt_us\":" + String(st.clock.bestRttUs);
  j += ",\"jitter_us\":" + String(st.clock.jitterUs);
  j += ",\"samples\":" + String(st.clock.good);
  j += ",\"rx_packets\":" + String(st.rxPackets);
  j += ",\"dropped_packets\":" + String(st.droppedPackets);
  j += ",\"strike_offset_ms\":" + String(config.strikeOffsetMs);
  j += ",\"peers\":[";
  SyncPeer peers[16];
  int n = syncPeers(peers, 16);
  for (int i = 0; i < n; i++) {
    const SyncPeer &p = peers[i];
    if (i) {
      j += ',';
    }
    j += "{\"name\":" + jsonString(p.name);
    j += ",\"ip\":" + jsonString(p.ip.toString());
    j += ",\"synced\":" + String(boolJson(p.synced));
    j += ",\"master\":" + String(boolJson(p.isMaster));
    j += ",\"conductor\":" + String(boolJson(p.isConductor));
    j += ",\"library_match\":" + String(boolJson(p.libraryMatch));
    j += ",\"playing\":" + String(boolJson(p.playing));
    j += ",\"song\":" + String(p.song);
    j += ",\"rssi\":" + String(p.rssi);
    j += ",\"rtt_us\":" + String(p.rttUs);
    j += ",\"jitter_us\":" + String(p.jitterUs);
    j += ",\"late_strikes\":" + String(p.lateStrikes);
    j += ",\"uptime_s\":" + String(p.uptimeS);
    j += ",\"age_ms\":" + String(p.ageMs);
    j += ",\"firmware\":" + jsonString(p.firmware) + "}";
  }
  j += "]}";
  return j;
}

static void handleStatus() {
  PlaybackStatus pb = playbackStatus();
  bool connected = WiFi.status() == WL_CONNECTED;
  String j;
  j.reserve(900);
  j += "{\"device\":{\"name\":" + jsonString(config.name);
  j += ",\"mac\":" + jsonString(WiFi.macAddress());
  j += ",\"firmware\":" + jsonString(FIRMWARE_VERSION);
  j += ",\"board\":" + jsonString(BOARD_NAME);
  j += ",\"uptime_s\":" + String((uint32_t)(millis() / 1000));
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"min_heap\":" + String(ESP.getMinFreeHeap());
  j += ",\"reset_reason\":" + jsonString(resetReason());
  j += ",\"pulse_ms\":" + String(bellsPulseMs());
  j += ",\"strikes\":" + String(bellsStrikeCount());
  j += "},\"wifi\":{\"state\":" + jsonString(netStateName());
  j += ",\"ssid\":" + jsonString(config.ssid);
  j += ",\"connected\":" + String(connected ? "true" : "false");
  j += ",\"ip\":" + (connected ? ipString(WiFi.localIP()) : String("null"));
  j += ",\"rssi\":" + (connected ? String(WiFi.RSSI()) : String("null"));
  j += ",\"channel\":" + String(WiFi.channel());
  j += ",\"hostname\":" + jsonString(config.name + ".local");
  j += ",\"ap\":" + String(netApActive() ? "true" : "false");
  j += ",\"ap_ssid\":" + jsonString(netApSsid());
  j += ",\"ap_clients\":" + String(netApActive() ? WiFi.softAPgetStationNum() : 0);
  j += "},\"playback\":{\"playing\":" + String(pb.playing ? "true" : "false");
  j += ",\"song\":" + String(pb.song);
  j += ",\"song_name\":" + jsonString(songName(pb.song));
  j += ",\"mode\":" + jsonString(playModeName(pb.mode));
  j += ",\"frame\":" + String(pb.frame);
  j += ",\"frames\":" + String(songFrameCount(pb.song));
  j += ",\"step_ms\":" + String(songStepMs(pb.song));
  j += ",\"late_strikes\":" + String(pb.lateStrikes);
  j += ",\"max_late_us\":" + String(pb.maxLateUs);
  j += "},\"sync\":" + syncJson();
  char crc[12];
  snprintf(crc, sizeof(crc), "%08lX", (unsigned long)songLibraryCrc());
  j += ",\"library\":{\"songs\":" + String(songCount());
  j += ",\"crc\":" + jsonString(crc) + "}}";
  sendJson(j);
}

static void sendResult(const char *err);

static String uidJson(uint32_t id) {
  char buf[12];
  snprintf(buf, sizeof(buf), "\"%08lx\"", (unsigned long)id);
  return buf;
}

static void handleSongs() {
  String j = "[";
  for (int i = 0; i < songCount(); i++) {
    if (i) {
      j += ',';
    }
    j += "{\"id\":" + String(i) + ",\"name\":" + jsonString(songName(i)) +
         ",\"frames\":" + String(songFrameCount(i)) + ",\"step_ms\":" + String(songStepMs(i));
    if (songIsUser(i)) {
      j += ",\"user\":true,\"uid\":" + uidJson(songUserId(i));
    }
    j += "}";
  }
  j += "]";
  sendJson(j);
}

// --- Composed songs: /api/user-songs/<uid>, uid in hex ------------------------

static uint16_t frameBuf[MAX_SONG_FRAMES];

// The uid path argument, or 0 (after replying 404) if no such song.
static uint32_t userSongArg() {
  String arg = server.pathArg(0);
  char *end;
  uint32_t id = strtoul(arg.c_str(), &end, 16);
  if (arg.isEmpty() || *end || songFindUser(id) < 0) {
    sendError(404, "unknown song");
    return 0;
  }
  return id;
}

static void handleUserSongGet() {
  uint32_t id = userSongArg();
  if (!id) {
    return;
  }
  int s = songFindUser(id);
  uint16_t n = songFrameCount(s);
  String j;
  j.reserve(80 + n * 5);
  j += "{\"uid\":" + uidJson(id) + ",\"song\":" + String(s) + ",\"name\":" + jsonString(songName(s));
  j += ",\"step_ms\":" + String(songStepMs(s)) + ",\"frames\":[";
  for (uint16_t i = 0; i < n; i++) {
    if (i) {
      j += ',';
    }
    j += songFrame(s, i);
  }
  j += "]}";
  sendJson(j);
}

// The song file as stored, for peers that need it to play along.
static void handleUserSongRaw() {
  uint32_t id = userSongArg();
  if (!id) {
    return;
  }
  File f = LittleFS.open(songUserPath(id));
  if (!f) {
    sendError(500, "couldn't read the song");
    return;
  }
  server.streamFile(f, "application/octet-stream");
  f.close();
}

// Form fields: name, step_ms, and frames as comma-separated hex ROM words.
static void saveUserSong(uint32_t id) {
  const String &list = server.arg("frames");
  const char *p = list.c_str();
  int n = 0;
  while (*p) {
    char *end;
    unsigned long w = strtoul(p, &end, 16);
    if (end == p || n == MAX_SONG_FRAMES || w > 0xFFFF || (*end && *end != ',')) {
      sendError(400, "frames must be up to 4000 comma-separated hex words");
      return;
    }
    frameBuf[n++] = w;
    p = *end ? end + 1 : end;
  }
  String name = server.arg("name");
  name.trim();
  long stepMs = server.arg("step_ms").toInt();
  const char *err = songSaveUser(&id, name.c_str(), constrain(stepMs, 0, 0xFFFF), frameBuf, n);
  if (err) {
    sendError(400, err);
    return;
  }
  sendJson("{\"ok\":true,\"uid\":" + uidJson(id) + ",\"song\":" + String(songFindUser(id)) + "}");
}

static void handleUserSongUpdate() {
  uint32_t id = userSongArg();
  if (id) {
    saveUserSong(id);
  }
}

static void handleUserSongDelete() {
  uint32_t id = userSongArg();
  if (id) {
    sendResult(songDeleteUser(id));
  }
}

// Command result: 409 when sync can't take the command right now.
static void sendResult(const char *err) {
  if (err) {
    sendError(409, err);
  } else {
    sendOk();
  }
}

// Optional "mode" argument shared by play and mode.
static bool parseMode(PlayMode *mode, bool *given) {
  *given = server.hasArg("mode");
  if (*given && !playModeParse(server.arg("mode").c_str(), mode)) {
    sendError(400, "mode must be single, loop or all");
    return false;
  }
  return true;
}

static void handlePlay() {
  PlayMode mode;
  bool modeGiven;
  if (!parseMode(&mode, &modeGiven)) {
    return;
  }
  int song = server.hasArg("song") ? server.arg("song").toInt() : -1;
  if (server.hasArg("song") && (song < 0 || song >= songCount())) {
    sendError(400, "unknown song");
    return;
  }
  if (modeGiven) {
    playbackSetMode(mode);
  }
  sendResult(syncPlay(song, server.hasArg("from") ? server.arg("from").toInt() : 0));
}

static void handleMode() {
  PlayMode mode;
  bool modeGiven;
  if (!parseMode(&mode, &modeGiven)) {
    return;
  }
  if (!modeGiven) {
    sendError(400, "missing mode");
    return;
  }
  sendResult(syncSetMode(mode));
}

static void handleStrike() {
  long word = server.arg("word").toInt();
  if (word <= 0 || word > 0x0FFF) {
    sendError(400, "word must be 1-4095");
    return;
  }
  bellsStrike(word);
  sendOk();
}

static void handleConfigGet() {
  sendJson("{\"name\":" + jsonString(config.name) + ",\"pulse_ms\":" + String(config.pulseMs) +
           ",\"ap_channel\":" + String(config.apChannel) + ",\"group\":" + String(config.group) +
           ",\"strike_offset_ms\":" + String(config.strikeOffsetMs) + "}");
}

static void handleConfigPost() {
  bool renamed = false;
  if (server.hasArg("name")) {
    String name = configSanitizeName(server.arg("name"));
    if (name.isEmpty()) {
      sendError(400, "name needs letters or digits");
      return;
    }
    renamed = name != config.name;
    config.name = name;
  }
  if (server.hasArg("pulse_ms")) {
    long ms = server.arg("pulse_ms").toInt();
    if (ms < 5 || ms > 500) {
      sendError(400, "pulse must be 5-500 ms");
      return;
    }
    config.pulseMs = ms;
    bellsSetPulseMs(ms);
  }
  if (server.hasArg("ap_channel")) {
    long ch = server.arg("ap_channel").toInt();
    if (ch < 1 || ch > 13) {
      sendError(400, "channel must be 1-13");
      return;
    }
    config.apChannel = ch;
  }
  if (server.hasArg("group")) {
    long group = server.arg("group").toInt();
    if (group < 1 || group > 255) {
      sendError(400, "group must be 1-255");
      return;
    }
    if (group != config.group) {
      config.group = group;
      syncReset();
    }
  }
  if (server.hasArg("strike_offset_ms")) {
    long ms = server.arg("strike_offset_ms").toInt();
    if (ms < -100 || ms > 100) {
      sendError(400, "strike offset must be -100 to 100 ms");
      return;
    }
    syncSetStrikeOffset(ms);
  }
  configSave();
  if (renamed) {
    netRename();
  }
  handleConfigGet();
}

static void handleWifiScan() {
  sendJson(netScanJson());
}

static void handleWifiPost() {
  String ssid = server.arg("ssid");
  if (ssid.isEmpty() || ssid.length() > 32) {
    sendError(400, "ssid must be 1-32 characters");
    return;
  }
  String pass = server.arg("pass");
  if (pass.length() > 0 && (pass.length() < 8 || pass.length() > 63)) {
    sendError(400, "password must be 8-63 characters, or empty for an open network");
    return;
  }
  pendingSsid = ssid;
  pendingPass = pass;
  pending = PENDING_CONNECT;
  pendingAt = millis();
  sendOk();
}

static void handleWifiForget() {
  pending = PENDING_FORGET;
  pendingAt = millis();
  sendOk();
}

// Captive portal: on the setup AP, send any unknown host or path to the page.
// Phones probe URLs like /generate_204 and /hotspot-detect.html.
static void handleNotFound() {
  if (netApActive() && server.hostHeader() != WiFi.softAPIP().toString()) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "not found");
}

void webBegin() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/compose", HTTP_GET, [] { server.send_P(200, "text/html", COMPOSER_HTML); });
  server.on("/api/songs", HTTP_GET, handleSongs);
  server.on("/api/user-songs", HTTP_POST, [] { saveUserSong(0); });
  server.on(UriBraces("/api/user-songs/{}/raw"), HTTP_GET, handleUserSongRaw);
  server.on(UriBraces("/api/user-songs/{}"), HTTP_GET, handleUserSongGet);
  server.on(UriBraces("/api/user-songs/{}"), HTTP_POST, handleUserSongUpdate);
  server.on(UriBraces("/api/user-songs/{}"), HTTP_DELETE, handleUserSongDelete);
  server.on("/api/play", HTTP_POST, handlePlay);
  server.on("/api/stop", HTTP_POST, [] { sendResult(syncStop()); });
  server.on("/api/next", HTTP_POST, [] { sendResult(syncNext()); });
  server.on("/api/mode", HTTP_POST, handleMode);
  server.on("/api/strike", HTTP_POST, handleStrike);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi", HTTP_POST, handleWifiPost);
  server.on("/api/wifi/forget", HTTP_POST, handleWifiForget);
  server.onNotFound(handleNotFound);
  server.begin();
}

void webService() {
  server.handleClient();
  if (pending != PENDING_NONE && millis() - pendingAt >= PENDING_DELAY_MS) {
    PendingAction action = pending;
    pending = PENDING_NONE;
    if (action == PENDING_CONNECT) {
      netConnect(pendingSsid, pendingPass);
    } else {
      netForget();
    }
  }
}
