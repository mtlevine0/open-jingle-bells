#include "console.h"
#include "bells.h"
#include "board.h"
#include "config.h"
#include "net.h"
#include "playback.h"
#include "sync.h"
#include "version.h"
#include <Arduino.h>
#include <WiFi.h>

static void printHelp() {
  Serial.println("Commands:");
  Serial.println("  strike <word>       strike a 12-bit ROM word (hex 0x.. or decimal)");
  Serial.println("  note <name> [...]   strike bells by name, together as a chord");
  Serial.println("  pulse [ms]          get or set strike length (5-500 ms, saved)");
  Serial.println("  songs               list the song library");
  Serial.println("  play [n]            play song n (default: the last one)");
  Serial.println("  stop | next");
  Serial.println("  mode single|loop|all");
  Serial.println("  sync                show group sync: roles, clock, peers");
  Serial.println("  drop <pct>          discard that % of received sync packets (testing)");
  Serial.println("  wifi                show Wi-Fi state");
  Serial.println("  wifi <ssid> [pass]  join a network (an SSID with spaces needs the page)");
  Serial.println("  wifi forget         clear credentials, back to the setup AP");
  Serial.println("  name [name]         get or set the unit name (<name>.local)");
#ifdef PLAYER_BOARD
  Serial.println("  power on|off        switch module 5V on edge pin 10");
#endif
  Serial.println("  notes               list bell names and their bits");
  Serial.println("  status              show settings and playback");
  Serial.println("  help");
}

static void printStatus() {
  PlaybackStatus pb = playbackStatus();
  Serial.printf("OK %s fw %s board %s mac %s\n", config.name.c_str(), FIRMWARE_VERSION, BOARD_NAME,
                WiFi.macAddress().c_str());
  Serial.printf("  wifi %s ssid '%s' ip %s rssi %d channel %d ap %s\n", netStateName(),
                config.ssid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI(), (int)WiFi.channel(),
                netApActive() ? netApSsid().c_str() : "off");
  Serial.printf("  %s song %d '%s' frame %ld/%u mode %s\n", pb.playing ? "playing" : "stopped",
                pb.song, songName(pb.song), (long)pb.frame, songFrameCount(pb.song), playModeName(pb.mode));
  Serial.printf("  pulse %u ms, strikes %lu, late %lu, worst %.2f ms, library %d songs %08lX\n",
                bellsPulseMs(), (unsigned long)bellsStrikeCount(), (unsigned long)pb.lateStrikes,
                pb.maxLateUs / 1000.0, songCount(), (unsigned long)songLibraryCrc());
}

// Prints an ERR line for a command error; true when the command succeeded.
static bool reply(const char *err) {
  if (err) {
    Serial.printf("ERR %s\n", err);
    return false;
  }
  return true;
}

static void printSync() {
  SyncStatus st = syncStatus();
  if (!st.active) {
    Serial.println("OK sync off: not on Wi-Fi, playing alone");
    return;
  }
  Serial.printf("OK group %u, %s, %s%s\n", config.group, st.synced ? "synced" : "syncing",
                st.isMaster ? "clock master" : "following clock master ",
                st.isMaster ? "" : st.masterName.c_str());
  Serial.printf("  conductor %s (epoch %lu)%s%s\n", st.conductorKnown ? st.conductorName.c_str() : "none",
                (unsigned long)st.epoch, st.isConductor ? ", this unit" : "",
                st.libraryMismatch ? ", LIBRARY MISMATCH" : "");
  Serial.printf("  clock offset %lld us, error %lld us, best rtt %ld us, jitter %ld us, %u samples\n",
                (long long)st.clock.offsetUs, (long long)st.clock.errorUs, (long)st.clock.bestRttUs,
                (long)st.clock.jitterUs, st.clock.good);
  Serial.printf("  packets rx %lu, dropped %lu (drop %u%%), strike offset %d ms\n",
                (unsigned long)st.rxPackets, (unsigned long)st.droppedPackets, st.dropPercent,
                config.strikeOffsetMs);
  SyncPeer peers[16];
  int n = syncPeers(peers, 16);
  for (int i = 0; i < n; i++) {
    const SyncPeer &p = peers[i];
    Serial.printf("  peer %-20s %-15s %s%s%s rtt %ld us jitter %ld us late %lu rssi %d%s\n", p.name.c_str(),
                  p.ip.toString().c_str(), p.synced ? "synced" : "syncing", p.isMaster ? " master" : "",
                  p.isConductor ? " conductor" : "", (long)p.rttUs, (long)p.jitterUs,
                  (unsigned long)p.lateStrikes, p.rssi, p.libraryMatch ? "" : " LIBRARY MISMATCH");
  }
}

static void handleCommand(char *line) {
  char *cmd = strtok(line, " \t");
  if (cmd == NULL) {
    return;
  }
  char *arg = strtok(NULL, " \t");

  if (strcasecmp(cmd, "strike") == 0 || strcasecmp(cmd, "s") == 0) {
    if (arg == NULL) {
      Serial.println("ERR missing word");
      return;
    }
    char *end;
    unsigned long word = strtoul(arg, &end, 0);
    if (*end != '\0' || word == 0 || word > 0x0FFF) {
      Serial.println("ERR word must be 1-0xFFF");
      return;
    }
    bellsStrike(word);
    Serial.printf("OK 0x%03lX\n", word);
  } else if (strcasecmp(cmd, "note") == 0 || strcasecmp(cmd, "n") == 0) {
    uint16_t word = 0;
    for (; arg != NULL; arg = strtok(NULL, " \t,")) {
      int bit = bellsNoteBit(arg);
      if (bit < 0) {
        Serial.printf("ERR unknown note %s\n", arg);
        return;
      }
      word |= 1 << bit;
    }
    if (word == 0) {
      Serial.println("ERR missing note");
      return;
    }
    bellsStrike(word);
    Serial.printf("OK 0x%03X\n", word);
  } else if (strcasecmp(cmd, "pulse") == 0) {
    if (arg != NULL) {
      long ms = strtol(arg, NULL, 10);
      if (ms < 5 || ms > 500) {
        Serial.println("ERR pulse must be 5-500 ms");
        return;
      }
      config.pulseMs = ms;
      bellsSetPulseMs(ms);
      configSave();
    }
    Serial.printf("OK pulse %u\n", bellsPulseMs());
  } else if (strcasecmp(cmd, "songs") == 0) {
    for (int i = 0; i < songCount(); i++) {
      uint32_t secs = (uint32_t)songFrameCount(i) * songStepMs(i) / 1000;
      Serial.printf("  %2d  %-40s %4u frames  %3u ms  %lu:%02lu\n", i, songName(i), songFrameCount(i),
                    songStepMs(i), (unsigned long)(secs / 60), (unsigned long)(secs % 60));
    }
    Serial.println("OK");
  } else if (strcasecmp(cmd, "play") == 0 || strcasecmp(cmd, "p") == 0) {
    int song = arg ? atoi(arg) : -1;
    if (arg && (song < 0 || song >= songCount())) {
      Serial.printf("ERR song must be 0-%d\n", songCount() - 1);
      return;
    }
    if (reply(syncPlay(song))) {
      Serial.printf("OK playing %d %s\n", playbackStatus().song, songName(playbackStatus().song));
    }
  } else if (strcasecmp(cmd, "stop") == 0) {
    if (reply(syncStop())) {
      Serial.println("OK stopping");
    }
  } else if (strcasecmp(cmd, "next") == 0) {
    if (reply(syncNext())) {
      Serial.printf("OK playing %d %s\n", playbackStatus().song, songName(playbackStatus().song));
    }
  } else if (strcasecmp(cmd, "mode") == 0) {
    PlayMode mode;
    if (arg == NULL || !playModeParse(arg, &mode)) {
      Serial.println("ERR usage: mode single|loop|all");
      return;
    }
    if (reply(syncSetMode(mode))) {
      Serial.printf("OK mode %s\n", playModeName(mode));
    }
  } else if (strcasecmp(cmd, "sync") == 0) {
    printSync();
  } else if (strcasecmp(cmd, "drop") == 0) {
    if (arg == NULL) {
      Serial.println("ERR usage: drop <0-100>");
      return;
    }
    syncSetDropPercent(atoi(arg));
    Serial.printf("OK dropping %u%% of received sync packets\n", syncStatus().dropPercent);
  } else if (strcasecmp(cmd, "wifi") == 0) {
    if (arg == NULL) {
      Serial.printf("OK %s ssid '%s' ip %s ap %s\n", netStateName(), config.ssid.c_str(),
                    WiFi.localIP().toString().c_str(), netApActive() ? netApSsid().c_str() : "off");
    } else if (strcasecmp(arg, "forget") == 0) {
      netForget();
      Serial.printf("OK forgotten; join %s to set up\n", netApSsid().c_str());
    } else {
      char *pass = strtok(NULL, "");
      netConnect(arg, pass ? pass : "");
      Serial.printf("OK joining %s\n", arg);
    }
  } else if (strcasecmp(cmd, "name") == 0) {
    if (arg != NULL) {
      String name = configSanitizeName(arg);
      if (name.isEmpty()) {
        Serial.println("ERR name needs letters or digits");
        return;
      }
      config.name = name;
      configSave();
      netRename();
    }
    Serial.printf("OK name %s (%s.local)\n", config.name.c_str(), config.name.c_str());
#ifdef PLAYER_BOARD
  } else if (strcasecmp(cmd, "power") == 0) {
    if (arg != NULL && strcasecmp(arg, "on") == 0) {
      digitalWrite(MOD_PWR_EN_PIN, HIGH);
    } else if (arg != NULL && strcasecmp(arg, "off") == 0) {
      digitalWrite(MOD_PWR_EN_PIN, LOW);
    } else {
      Serial.println("ERR usage: power on|off");
      return;
    }
    Serial.printf("OK power %s\n", digitalRead(MOD_PWR_EN_PIN) ? "on" : "off");
#endif
  } else if (strcasecmp(cmd, "notes") == 0) {
    for (int i = 0; i < 12; i++) {
      Serial.printf("  %-4s bit %2d  0x%03X  GPIO%d\n", NOTE_NAMES[i], i, 1 << i, DATA_PINS[i]);
    }
    Serial.println("OK");
  } else if (strcasecmp(cmd, "status") == 0) {
    printStatus();
  } else if (strcasecmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
    Serial.println("OK");
  } else {
    Serial.printf("ERR unknown command %s\n", cmd);
  }
}

void consoleService() {
  static char line[128];
  static size_t len = 0;
  static uint32_t lastMs = 0;
  // Drop a partial line left idle, such as our own log text echoed back by a
  // host tty that hadn't been put in raw mode yet; it would otherwise prefix
  // the next command.
  if (len > 0 && Serial.available() && millis() - lastMs > 500) {
    len = 0;
  }
  while (Serial.available()) {
    char c = Serial.read();
    lastMs = millis();
    if (c == '\r' || c == '\n') {
      if (len > 0) {
        line[len] = '\0';
        handleCommand(line);
        len = 0;
      }
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}
