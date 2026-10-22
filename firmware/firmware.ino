// Minstrel: bell unit firmware with a compiled-in song library, Wi-Fi setup,
// a control page and group sync over UDP. See docs/minstrel-firmware-plan.md.
//
// Build and flash in one step (a bare "upload" flashes whatever was compiled
// last, which may be the other board's build):
//   arduino-cli compile -u -p /dev/ttyACM0 -b esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=default_8MB
// Add --build-property compiler.cpp.extra_flags=-DPLAYER_BOARD for the
// merrie-minstrel-player board. The boot banner and "status" name the board.

#include "bells.h"
#include "board.h"
#include "config.h"
#include "console.h"
#include "net.h"
#include "playback.h"
#include "sync.h"
#include "web.h"

static const uint32_t DEBOUNCE_MS = 30;
static const uint32_t HOLD_FORGET_MS = 5000;

// Button: a short press toggles play/stop; holding it 5 s forgets Wi-Fi.
// On the choir board this is S1 (BOOT); it only straps download mode while
// held through reset, so the hold is timed after boot.
static void serviceButton() {
  static bool stable = false, last = false, held = false;
  static uint32_t changedAt = 0, pressedAt = 0;
  bool down = digitalRead(BUTTON_PIN) == LOW;
  uint32_t now = millis();
  if (down != last) {
    last = down;
    changedAt = now;
  }
  if (now - changedAt < DEBOUNCE_MS || down == stable) {
    if (stable && !held && now - pressedAt >= HOLD_FORGET_MS) {
      held = true;
      Serial.println("button: held, forgetting Wi-Fi");
      netForget();
    }
    return;
  }
  stable = down;
  if (down) {
    pressedAt = now;
    held = false;
  } else if (!held) {
    const char *err = syncToggle();
    if (err) {
      Serial.printf("button: %s\n", err);
    }
  }
}

// Strikes flash the LED. While idle in setup mode it blinks slowly instead.
static void serviceLed() {
  static bool blinking = false;
  bool want = netApActive() && netState() != NET_CONNECTED && !playbackStatus().playing;
  if (want) {
    digitalWrite(LED_PIN, (millis() / 500) % 2);
    blinking = true;
  } else if (blinking) {
    digitalWrite(LED_PIN, LOW);
    blinking = false;
  }
}

void setup() {
  bellsBegin();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);

  configBegin();
  bellsSetPulseMs(config.pulseMs);
  playbackBegin();
  netBegin();
  syncBegin();
  webBegin();

  Serial.printf("minstrel %s ready on %s; type 'help'\n", config.name.c_str(), BOARD_NAME);
}

void loop() {
  consoleService();
  netService();
  syncService();
  webService();
  serviceButton();
  serviceLed();
  delay(1);
}
