#pragma once
#include <Arduino.h>

// Wi-Fi state machine.
//
//   SETUP       no credentials: open AP "JingleBells-XXXX" with a captive portal
//   CONNECTING  joining the saved network (20 s)
//   CONNECTED   on the network; AP off, mDNS <name>.local
//   FALLBACK    joining failed: setup AP back up, STA retried while no phone
//               is connected to the AP
enum NetState : uint8_t { NET_SETUP, NET_CONNECTING, NET_CONNECTED, NET_FALLBACK };

void netBegin();
void netService();
// Save credentials and start joining. The AP stays up until the join works,
// so a phone on the setup page sees the result.
void netConnect(const String &ssid, const String &pass);
// Clear credentials and go back to the setup AP.
void netForget();
// Re-register mDNS after a name change.
void netRename();

NetState netState();
const char *netStateName();
bool netApActive();
String netApSsid();
// Starts an async scan if none is cached or the cache is stale; returns
// the last results as a JSON array, or "null" while the first scan runs.
String netScanJson();
