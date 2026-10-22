#include "playback.h"
#include "bells.h"
#include "songs.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <strings.h>

static const int64_t LEAD_IN_US = 100000;   // first frame after play
static const int64_t SPIN_US = 2000;        // busy-wait the last stretch
static const int64_t SKIP_US = 50000;       // later than this: drop the frame

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t task;

// Shared state, guarded by mux.
static bool playing = false;
static int song = 0;
static PlayMode mode = MODE_SINGLE;
static int64_t startUs = 0;
static int64_t stopAtUs = INT64_MAX;
static int32_t nextFrame = 0;
static uint32_t generation = 0;  // bumped on every play/stop/next
static uint32_t lateStrikes = 0;
static int32_t maxLateUs = 0;

static int64_t frameTime(int s, int64_t start, int32_t frame) {
  return start + (int64_t)frame * SONGS[s].stepMs * 1000;
}

// Called with mux held: the song ended, pick what plays next.
static void advanceLocked() {
  int64_t endUs = frameTime(song, startUs, SONGS[song].count);
  if (mode == MODE_SINGLE) {
    playing = false;
    return;
  }
  if (mode == MODE_PLAY_ALL) {
    song = (song + 1) % SONG_COUNT;
  }
  startUs = endUs + SONG_GAP_US;
  nextFrame = 0;
}

static void schedulerTask(void *) {
  for (;;) {
    portENTER_CRITICAL(&mux);
    if (playing && nextFrame >= SONGS[song].count) {
      advanceLocked();
    }
    if (playing && frameTime(song, startUs, nextFrame) >= stopAtUs) {
      playing = false;
      stopAtUs = INT64_MAX;
    }
    bool isPlaying = playing;
    int s = song;
    int32_t frame = nextFrame;
    uint32_t gen = generation;
    int64_t due = isPlaying ? frameTime(s, startUs, frame) : 0;
    portEXIT_CRITICAL(&mux);

    if (!isPlaying) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }

    int64_t wait = due - esp_timer_get_time();
    if (wait < -SKIP_US) {
      // Far behind (joined mid-song, or stalled): move to the frame that is
      // due now instead of firing a burst of stale strikes.
      portENTER_CRITICAL(&mux);
      if (gen == generation) {
        int64_t stepUs = (int64_t)SONGS[s].stepMs * 1000;
        nextFrame = (esp_timer_get_time() - startUs) / stepUs + 1;
      }
      portEXIT_CRITICAL(&mux);
      continue;
    }
    if (wait > SPIN_US + 1000) {
      // Sleep until about SPIN_US before the frame; a play/stop wakes us early.
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS((wait - SPIN_US) / 1000));
      continue;
    }
    while (esp_timer_get_time() < due) {
    }
    int64_t late = esp_timer_get_time() - due;

    portENTER_CRITICAL(&mux);
    bool current = gen == generation;
    if (current) {
      nextFrame = frame + 1;
      if (late > LATE_US) {
        lateStrikes++;
      }
      if (late > maxLateUs) {
        maxLateUs = late;
      }
    }
    portEXIT_CRITICAL(&mux);
    if (current) {
      bellsStrike(SONGS[s].frames[frame]);
    }
  }
}

void playbackBegin() {
  // loop() runs at priority 1 on core 1; stay above it.
  xTaskCreatePinnedToCore(schedulerTask, "playback", 4096, NULL, 5, &task, 1);
}

static void wake() {
  xTaskNotifyGive(task);
}

bool playbackPlay(int s, int64_t start) {
  if (s < 0 || s >= SONG_COUNT) {
    return false;
  }
  if (start == 0) {
    start = esp_timer_get_time() + LEAD_IN_US;
  }
  portENTER_CRITICAL(&mux);
  song = s;
  startUs = start;
  stopAtUs = INT64_MAX;
  nextFrame = 0;
  playing = true;
  generation++;
  portEXIT_CRITICAL(&mux);
  wake();
  return true;
}

void playbackStop() {
  portENTER_CRITICAL(&mux);
  playing = false;
  stopAtUs = INT64_MAX;
  generation++;
  portEXIT_CRITICAL(&mux);
  wake();
}

void playbackStopAt(int64_t stopUs) {
  if (stopUs <= esp_timer_get_time()) {
    playbackStop();
    return;
  }
  portENTER_CRITICAL(&mux);
  if (playing) {
    stopAtUs = stopUs;
  }
  portEXIT_CRITICAL(&mux);
  wake();
}

// The frame index stays: the scheduler re-reads startUs each pass, so frames
// simply fall due a little earlier or later. No generation bump, so a strike
// already spinning still fires.
void playbackRetime(int64_t deltaUs) {
  portENTER_CRITICAL(&mux);
  startUs += deltaUs;
  if (stopAtUs != INT64_MAX) {
    stopAtUs += deltaUs;
  }
  portEXIT_CRITICAL(&mux);
  wake();
}

void playbackNext() {
  portENTER_CRITICAL(&mux);
  int s = (song + 1) % SONG_COUNT;
  portEXIT_CRITICAL(&mux);
  playbackPlay(s);
}

void playbackSetMode(PlayMode m) {
  portENTER_CRITICAL(&mux);
  mode = m;
  portEXIT_CRITICAL(&mux);
}

PlaybackStatus playbackStatus() {
  PlaybackStatus st;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&mux);
  st.playing = playing;
  st.song = song;
  st.mode = mode;
  st.frame = nextFrame;
  st.startUs = startUs;
  st.stopAtUs = stopAtUs;
  if (playing && now < startUs) {
    st.frame = -(int32_t)((startUs - now) / (SONGS[song].stepMs * 1000) + 1);
  }
  st.lateStrikes = lateStrikes;
  st.maxLateUs = maxLateUs;
  portEXIT_CRITICAL(&mux);
  return st;
}

int songCount() {
  return SONG_COUNT;
}

const char *songName(int s) {
  return SONGS[s].name;
}

uint16_t songStepMs(int s) {
  return SONGS[s].stepMs;
}

uint16_t songFrameCount(int s) {
  return SONGS[s].count;
}

uint32_t songLibraryCrc() {
  return LIBRARY_CRC;
}

static const char *const MODE_NAMES[] = {"single", "loop", "all"};

const char *playModeName(PlayMode m) {
  return MODE_NAMES[m];
}

bool playModeParse(const char *name, PlayMode *m) {
  for (int i = 0; i < 3; i++) {
    if (strcasecmp(name, MODE_NAMES[i]) == 0) {
      *m = (PlayMode)i;
      return true;
    }
  }
  return false;
}
