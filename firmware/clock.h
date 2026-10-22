#pragma once
#include <stdint.h>

// Group time: groupNow() = esp_timer_get_time() + offset.
//
// The clock master serves group time and never changes its own offset, so
// group time stays continuous when mastership moves. Other units estimate the
// offset from NTP-style exchanges with the master: keep the last samples, trust
// the one with the smallest round trip, and slew toward it.
//
// Safe to call from the UDP task and loop().

int64_t clockGroupNow();
int64_t clockLocalToGroup(int64_t localUs);
int64_t clockGroupToLocal(int64_t groupUs);

// Forget the samples (the master changed). The offset itself stays, so group
// time doesn't jump; the next samples correct it.
void clockResetSamples();
// One exchange: t1/t4 local send/receive, t2/t3 master group-time receive/send.
void clockAddSample(int64_t t1, int64_t t2, int64_t t3, int64_t t4);
// Moves the offset toward the estimate: slewed once synced, a straight step
// while still converging. Returns the change applied (µs) so the caller can
// retime playback by its negative.
int64_t clockService(bool converging);

struct ClockStats {
  int64_t offsetUs;    // current group - local offset
  int64_t errorUs;     // estimate minus current offset (still to slew)
  int32_t bestRttUs;   // -1 with no samples
  int32_t jitterUs;    // spread of the good samples' offsets
  uint8_t good;        // samples within 3x the best RTT
};
ClockStats clockStats();
