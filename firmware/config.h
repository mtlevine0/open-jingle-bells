#pragma once
#include <Arduino.h>

// Settings kept in NVS (Preferences namespace "minstrel").
struct Config {
  String name;          // mDNS host name and the name shown on the page
  String ssid;          // empty: not configured, run the setup AP
  String pass;
  uint16_t pulseMs;     // strike length, 5-500 ms
  uint8_t apChannel;    // setup AP channel
  uint8_t group;        // sync group, 1-255; units only follow their own group
  int16_t strikeOffsetMs;  // -100..100; positive strikes this unit earlier
};

extern Config config;

void configBegin();
void configSave();
void configSaveWifi(const String &ssid, const String &pass);
// Lowercase letters, digits and dashes, 1-31 chars; "" if nothing usable.
String configSanitizeName(const String &raw);
String configDefaultName();
