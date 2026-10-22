#pragma once
#include <Arduino.h>
#include "clock.h"
#include "playback.h"

// Group sync over UDP on the LAN (see docs/minstrel-firmware-plan.md).
//
// Every unit broadcasts HELLO; the conductor (whoever issued the last command)
// broadcasts STATE, a song timeline in group time; each unit keeps its clock
// on the clock master's with unicast TIME exchanges, then maps the timeline
// onto local playback. Off the network, commands act on this unit alone.

void syncBegin();
void syncService();  // from loop()

// Commands from the page, console or button. Return NULL on success or an
// error message.
const char *syncPlay(int song);  // -1: the current song
const char *syncStop();
const char *syncNext();
const char *syncToggle();
const char *syncSetMode(PlayMode mode);
// Applies a new strike offset (ms) and shifts any running timeline to match.
void syncSetStrikeOffset(int16_t ms);

// Forget peers and the group timeline (after a group change).
void syncReset();
void syncSetDropPercent(uint8_t pct);  // testing: discard received packets

struct SyncPeer {
  String name;
  IPAddress ip;
  uint8_t mac[6];
  bool synced;
  bool isMaster;
  bool isConductor;
  bool libraryMatch;
  bool playing;
  uint8_t song;
  int8_t rssi;
  int32_t rttUs;
  int32_t jitterUs;
  uint32_t lateStrikes;
  uint32_t uptimeS;
  uint32_t ageMs;
  String firmware;
};

struct SyncStatus {
  bool active;          // on Wi-Fi and listening
  bool synced;          // clock locked to the group (or this unit is master)
  bool isMaster;
  bool isConductor;
  bool conductorKnown;  // a STATE was heard in the last 3 s (or we conduct)
  bool libraryMismatch; // the conductor's library differs from ours
  uint32_t epoch;
  String masterName;
  String conductorName;
  ClockStats clock;
  uint32_t rxPackets;
  uint32_t droppedPackets;
  uint8_t dropPercent;
  uint8_t peerCount;
};

SyncStatus syncStatus();
int syncPeers(SyncPeer *out, int max);
