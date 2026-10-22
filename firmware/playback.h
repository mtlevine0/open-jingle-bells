#pragma once
#include <stdint.h>

// Playback scheduler. A task pinned to core 1, above loop()'s priority,
// strikes each frame at startUs + frame * stepMs on esp_timer time, so web
// and Wi-Fi work in loop() can't delay a strike.
//
// The timeline is (song, startUs) in local esp_timer time. sync.cpp maps the
// group timeline onto it and shifts it with playbackRetime() as the clock
// estimate moves.

enum PlayMode : uint8_t { MODE_SINGLE, MODE_LOOP_SONG, MODE_PLAY_ALL };

struct PlaybackStatus {
  bool playing;
  int song;            // current (or last selected) song
  PlayMode mode;
  int32_t frame;       // next frame to strike; negative during the lead-in
  int64_t startUs;     // local time of frame 0 of the current song
  int64_t stopAtUs;    // pending scheduled stop, or INT64_MAX
  uint32_t lateStrikes;  // strikes that fired more than LATE_US after their time
  int32_t maxLateUs;
};

static const int64_t LATE_US = 2000;
// Silence between songs in repeat and play-all.
static const int64_t SONG_GAP_US = 1500000;

void playbackBegin();
// Starts song at local time startUs (0 = after a short lead-in).
bool playbackPlay(int song, int64_t startUs = 0);
void playbackStop();
// Stop at local time stopUs; frames due from then on are not struck.
void playbackStopAt(int64_t stopUs);
// Shift the timeline (start and any pending stop) by deltaUs without
// restarting it.
void playbackRetime(int64_t deltaUs);
void playbackNext();
void playbackSetMode(PlayMode mode);
PlaybackStatus playbackStatus();

// The compiled-in song library (songs.h, built by firmware/tools/build_songs.py).
int songCount();
const char *songName(int song);
uint16_t songStepMs(int song);
uint16_t songFrameCount(int song);
uint32_t songLibraryCrc();

const char *playModeName(PlayMode mode);
bool playModeParse(const char *name, PlayMode *mode);
