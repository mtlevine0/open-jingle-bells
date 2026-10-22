#include "bells.h"
#include "board.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <strings.h>

static SemaphoreHandle_t lock;
static esp_timer_handle_t pulseTimer;
static volatile uint16_t pulseMs = 40;
static volatile uint32_t strikeCount = 0;
static int64_t pulseEndUs = 0;
static bool active = false;

static void setBus(uint16_t word) {
  for (int i = 0; i < 12; i++) {
    digitalWrite(DATA_PINS[i], (word >> i) & 1);
  }
}

// Runs in the esp_timer task. A strike that landed while this callback
// waited for the lock moved pulseEndUs; re-arm instead of cutting it short.
static void endPulse(void *) {
  xSemaphoreTake(lock, portMAX_DELAY);
  if (active) {
    int64_t remaining = pulseEndUs - esp_timer_get_time();
    if (remaining > 200) {
      esp_timer_start_once(pulseTimer, remaining);
    } else {
      setBus(0);
      digitalWrite(LED_PIN, LOW);
      active = false;
    }
  }
  xSemaphoreGive(lock);
}

void bellsBegin() {
#ifdef PLAYER_BOARD
  // Boot-safe order from the schematic: translators off, then outputs low,
  // then enable the translators. Module power stays off; the bells are
  // driven from the translators' unswitched 5V side.
  pinMode(LVL_OE_PIN, OUTPUT);
  digitalWrite(LVL_OE_PIN, LOW);
  pinMode(MOD_PWR_EN_PIN, OUTPUT);
  digitalWrite(MOD_PWR_EN_PIN, LOW);
#endif
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  for (int i = 0; i < 12; i++) {
    pinMode(DATA_PINS[i], OUTPUT);
    digitalWrite(DATA_PINS[i], LOW);
  }
#ifdef PLAYER_BOARD
  delay(10);
  digitalWrite(LVL_OE_PIN, HIGH);
#endif

  lock = xSemaphoreCreateMutex();
  esp_timer_create_args_t args = {};
  args.callback = endPulse;
  args.name = "bell_pulse";
  esp_timer_create(&args, &pulseTimer);
}

void bellsStrike(uint16_t word) {
  word &= 0x0FFF;
  if (word == 0) {
    return;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  esp_timer_stop(pulseTimer);
  setBus(word);
  digitalWrite(LED_PIN, HIGH);
  active = true;
  pulseEndUs = esp_timer_get_time() + (int64_t)pulseMs * 1000;
  esp_timer_start_once(pulseTimer, (uint64_t)pulseMs * 1000);
  strikeCount = strikeCount + 1;
  xSemaphoreGive(lock);
}

void bellsSetPulseMs(uint16_t ms) {
  pulseMs = ms;
}

uint16_t bellsPulseMs() {
  return pulseMs;
}

uint32_t bellsStrikeCount() {
  return strikeCount;
}

int bellsNoteBit(const char *name) {
  for (int i = 0; i < 12; i++) {
    if (strcasecmp(name, NOTE_NAMES[i]) == 0 ||
        (NOTE_ALIASES[i][0] && strcasecmp(name, NOTE_ALIASES[i]) == 0)) {
      return i;
    }
  }
  return -1;
}
