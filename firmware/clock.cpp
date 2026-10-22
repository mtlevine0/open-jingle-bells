#include "clock.h"
#include <Arduino.h>
#include <esp_timer.h>

static const int WINDOW = 16;
static const int64_t STEP_THRESHOLD_US = 20000;  // jump straight there
static const int64_t SLEW_US_PER_S = 500;
static const int64_t SERVICE_MIN_US = 20000;

struct Sample {
  int64_t offset;
  int32_t rtt;
};

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t offset = 0;
static Sample samples[WINDOW];
static int count = 0;
static int head = 0;
static int64_t lastServiceUs = 0;

int64_t clockGroupNow() {
  return clockLocalToGroup(esp_timer_get_time());
}

int64_t clockLocalToGroup(int64_t localUs) {
  portENTER_CRITICAL(&mux);
  int64_t o = offset;
  portEXIT_CRITICAL(&mux);
  return localUs + o;
}

int64_t clockGroupToLocal(int64_t groupUs) {
  portENTER_CRITICAL(&mux);
  int64_t o = offset;
  portEXIT_CRITICAL(&mux);
  return groupUs - o;
}

void clockResetSamples() {
  portENTER_CRITICAL(&mux);
  count = 0;
  head = 0;
  portEXIT_CRITICAL(&mux);
}

void clockAddSample(int64_t t1, int64_t t2, int64_t t3, int64_t t4) {
  int64_t rtt = (t4 - t1) - (t3 - t2);
  if (rtt < 0 || rtt > 500000) {
    return;
  }
  Sample s = {((t2 - t1) + (t3 - t4)) / 2, (int32_t)rtt};
  portENTER_CRITICAL(&mux);
  samples[head] = s;
  head = (head + 1) % WINDOW;
  if (count < WINDOW) {
    count++;
  }
  portEXIT_CRITICAL(&mux);
}

// With mux held. Good samples have an RTT within 3x the best one.
static bool estimateLocked(int64_t *estimate, int32_t *bestRtt, int *good, int32_t *jitter) {
  if (count == 0) {
    return false;
  }
  int best = 0;
  for (int i = 1; i < count; i++) {
    if (samples[i].rtt < samples[best].rtt) {
      best = i;
    }
  }
  int32_t limit = samples[best].rtt * 3 + 500;
  int64_t lo = INT64_MAX, hi = INT64_MIN;
  int n = 0;
  for (int i = 0; i < count; i++) {
    if (samples[i].rtt <= limit) {
      n++;
      lo = min(lo, samples[i].offset);
      hi = max(hi, samples[i].offset);
    }
  }
  *estimate = samples[best].offset;
  *bestRtt = samples[best].rtt;
  *good = n;
  *jitter = (int32_t)(hi - lo);
  return true;
}

int64_t clockService(bool converging) {
  int64_t now = esp_timer_get_time();
  if (now - lastServiceUs < SERVICE_MIN_US) {
    return 0;
  }
  int64_t elapsed = lastServiceUs ? now - lastServiceUs : SERVICE_MIN_US;
  lastServiceUs = now;

  portENTER_CRITICAL(&mux);
  int64_t estimate;
  int32_t rtt, jitter;
  int good;
  int64_t applied = 0;
  if (estimateLocked(&estimate, &rtt, &good, &jitter)) {
    int64_t error = estimate - offset;
    if (converging || error > STEP_THRESHOLD_US || error < -STEP_THRESHOLD_US) {
      applied = error;
    } else {
      int64_t maxStep = elapsed * SLEW_US_PER_S / 1000000 + 1;
      applied = constrain(error, -maxStep, maxStep);
    }
    offset += applied;
  }
  portEXIT_CRITICAL(&mux);
  return applied;
}

ClockStats clockStats() {
  ClockStats st = {};
  st.bestRttUs = -1;
  int64_t estimate;
  int good = 0;
  portENTER_CRITICAL(&mux);
  st.offsetUs = offset;
  if (estimateLocked(&estimate, &st.bestRttUs, &good, &st.jitterUs)) {
    st.errorUs = estimate - offset;
  }
  portEXIT_CRITICAL(&mux);
  st.good = good;
  return st;
}
