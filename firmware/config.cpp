#include "config.h"
#include <Preferences.h>
#include <esp_mac.h>

Config config;
static Preferences prefs;

String configDefaultName() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char name[20];
  snprintf(name, sizeof(name), "minstrel-%02x%02x", mac[4], mac[5]);
  return name;
}

String configSanitizeName(const String &raw) {
  String name;
  for (size_t i = 0; i < raw.length() && name.length() < 31; i++) {
    char c = tolower(raw[i]);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      name += c;
    } else if ((c == '-' || c == ' ' || c == '_') && name.length() > 0 && !name.endsWith("-")) {
      name += '-';
    }
  }
  while (name.endsWith("-")) {
    name.remove(name.length() - 1);
  }
  return name;
}

void configBegin() {
  prefs.begin("minstrel", false);
  config.name = prefs.getString("name", "");
  if (config.name.isEmpty()) {
    config.name = configDefaultName();
  }
  config.ssid = prefs.getString("ssid", "");
  config.pass = prefs.getString("pass", "");
  config.pulseMs = prefs.getUShort("pulse", 40);
  config.apChannel = prefs.getUChar("apch", 6);
  config.group = prefs.getUChar("group", 1);
  config.strikeOffsetMs = prefs.getShort("strikeofs", 0);
}

void configSave() {
  prefs.putString("name", config.name);
  prefs.putUShort("pulse", config.pulseMs);
  prefs.putUChar("apch", config.apChannel);
  prefs.putUChar("group", config.group);
  prefs.putShort("strikeofs", config.strikeOffsetMs);
}

void configSaveWifi(const String &ssid, const String &pass) {
  config.ssid = ssid;
  config.pass = pass;
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
}
