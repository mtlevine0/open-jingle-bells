#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

const int DATA_PINS[12] = {
  4, 6, 15, 18, 3, 9, 46, 8, 17, 16, 7, 5
};

const int LED_PIN = 35;

uint8_t channel = 6; 

typedef struct struct_message {
    uint16_t noteByte;
} struct_message;

struct_message myData;

void OnDataRecv(const esp_now_recv_info_t * info, const uint8_t *incomingData, int len) {
  
  memcpy(&myData, incomingData, sizeof(myData));

  Serial.print("Note v2: ");
  Serial.println(myData.noteByte);
  Serial.println();
  writeBus(myData.noteByte);
}

// Output one 12-bit word
inline void writeBus(uint16_t word) {
  if (word != 0x00) {
    digitalWrite(LED_PIN, HIGH);
  }
  for (int i = 0; i < 12; i++) {
    digitalWrite(DATA_PINS[i], (word >> i) & 1);
  }
  delay(40);
  digitalWrite(LED_PIN, LOW);
  for (int i = 0; i < 12; i++) {
    digitalWrite(DATA_PINS[i], LOW);
  }

}
 
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }  
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  esp_now_register_recv_cb(OnDataRecv);

  for (int i = 0; i < 12; i++) {
    pinMode(DATA_PINS[i], OUTPUT);
    digitalWrite(DATA_PINS[i], LOW);
  }
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}
 
void loop() {

}