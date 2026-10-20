#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Bell player: strikes bells from ESP-NOW packets (same format as the
// transmitter sketch) or from line commands over USB serial.
//
// Words use the ROM bit order, so ROM dumps and transmitter packets work
// unchanged: bit i drives DATA_PINS[i].
//
// The default pin map is the merrie-minstrel-choir board. Define
// PLAYER_BOARD for the merrie-minstrel-player board, whose bell lines go
// through TXS0108E translators to the same edge pins.
#ifdef PLAYER_BOARD
const int DATA_PINS[12] = {
  38, 36, 48, 18, 16, 7, 15, 17, 8, 35, 37, 39
};
const int LED_PIN = 41;
const int LVL_OE_PIN = 4;
const int MOD_PWR_EN_PIN = 40;
#else
const int DATA_PINS[12] = {
  4, 6, 15, 18, 3, 9, 46, 8, 17, 16, 7, 5
};
const int LED_PIN = 35;
#endif

uint8_t channel = 6;

typedef struct struct_message {
    uint16_t noteByte;
} struct_message;

// Bell names in ROM bit order (see transmitter/rom_player.py).
const char *NOTE_NAMES[12] = {
  "E4", "D4", "C4", "E5", "Bb4", "B4", "C5", "D5", "A4", "G4", "F#4", "F4"
};

QueueHandle_t strikeQueue;
uint16_t pulseMs = 40;

uint16_t activeWord = 0;
uint32_t strikeStart = 0;

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len < (int)sizeof(struct_message)) {
    return;
  }
  struct_message msg;
  memcpy(&msg, incomingData, sizeof(msg));
  uint16_t word = msg.noteByte & 0x0FFF;
  Serial.printf("RX 0x%03X\n", word);
  if (word != 0) {
    xQueueSend(strikeQueue, &word, 0);
  }
}

void setBus(uint16_t word) {
  for (int i = 0; i < 12; i++) {
    digitalWrite(DATA_PINS[i], (word >> i) & 1);
  }
}

// Strikes are non-blocking so serial input keeps being read during a pulse.
void serviceStrikes() {
  if (activeWord != 0) {
    if (millis() - strikeStart < pulseMs) {
      return;
    }
    setBus(0);
    digitalWrite(LED_PIN, LOW);
    activeWord = 0;
  }
  uint16_t word;
  if (xQueueReceive(strikeQueue, &word, 0) == pdTRUE) {
    activeWord = word;
    strikeStart = millis();
    setBus(word);
    digitalWrite(LED_PIN, HIGH);
  }
}

int noteBit(const char *name) {
  for (int i = 0; i < 12; i++) {
    if (strcasecmp(name, NOTE_NAMES[i]) == 0) {
      return i;
    }
  }
  return -1;
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  strike <word>       strike a 12-bit ROM word (hex 0x.. or decimal)");
  Serial.println("  note <name> [...]   strike bells by name, together as a chord");
  Serial.println("  pulse [ms]          get or set strike length (5-500 ms)");
#ifdef PLAYER_BOARD
  Serial.println("  power on|off        switch module 5V on edge pin 10");
#endif
  Serial.println("  notes               list bell names and their bits");
  Serial.println("  status              show settings");
  Serial.println("  help");
}

void queueStrike(uint16_t word) {
  if (xQueueSend(strikeQueue, &word, 0) == pdTRUE) {
    Serial.printf("OK 0x%03X\n", word);
  } else {
    Serial.println("ERR queue full");
  }
}

void handleCommand(char *line) {
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
    if (*end != '\0' || word > 0x0FFF) {
      Serial.println("ERR word must be 0-0xFFF");
      return;
    }
    queueStrike(word);
  } else if (strcasecmp(cmd, "note") == 0 || strcasecmp(cmd, "n") == 0) {
    uint16_t word = 0;
    for (; arg != NULL; arg = strtok(NULL, " \t,")) {
      int bit = noteBit(arg);
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
    queueStrike(word);
  } else if (strcasecmp(cmd, "pulse") == 0) {
    if (arg != NULL) {
      long ms = strtol(arg, NULL, 10);
      if (ms < 5 || ms > 500) {
        Serial.println("ERR pulse must be 5-500 ms");
        return;
      }
      pulseMs = ms;
    }
    Serial.printf("OK pulse %u\n", pulseMs);
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
    Serial.printf("OK player mac %s channel %u pulse %u\n",
                  WiFi.macAddress().c_str(), channel, pulseMs);
  } else if (strcasecmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
    Serial.println("OK");
  } else {
    Serial.printf("ERR unknown command %s\n", cmd);
  }
}

void serviceSerial() {
  static char line[96];
  static size_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
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

void setup() {
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

  Serial.begin(115200);
  strikeQueue = xQueueCreate(32, sizeof(uint16_t));

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
  } else {
    // The transmitter sends with the long-range protocol enabled.
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_now_register_recv_cb(OnDataRecv);
  }

  Serial.println("merrie-minstrel-player ready; type 'help'");
}

void loop() {
  serviceSerial();
  serviceStrikes();
}
