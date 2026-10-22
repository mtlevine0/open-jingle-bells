#include "net.h"
#include "config.h"
#include "json.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_mac.h>

static const uint32_t CONNECT_TIMEOUT_MS = 20000;
static const uint32_t RETRY_INTERVAL_MS = 30000;  // fallback: between attempts
static const uint32_t RETRY_WINDOW_MS = 15000;    // fallback: length of an attempt
static const uint32_t AP_GRACE_MS = 30000;        // keep the AP after joining
static const uint32_t SCAN_STALE_MS = 15000;

static DNSServer dns;
static NetState state = NET_SETUP;
static bool apActive = false;
static bool mdnsActive = false;
static uint32_t stateSince = 0;
static uint32_t apStopAt = 0;
static bool retrying = false;
static uint32_t retryAt = 0;
static String scanCache;
static uint32_t scanCachedAt = 0;

String netApSsid() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char ssid[20];
  snprintf(ssid, sizeof(ssid), "Minstrel-%02X%02X", mac[4], mac[5]);
  return ssid;
}

static void setState(NetState s) {
  state = s;
  stateSince = millis();
  Serial.printf("net: %s\n", netStateName());
}

// Applied after every mode change; the driver resets some of it.
static void radioSettings() {
  WiFi.setSleep(false);  // modem sleep misses broadcasts (and later ESP-NOW)
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
}

static void startAp() {
  if (apActive) {
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(netApSsid().c_str(), NULL, config.apChannel);
  radioSettings();
  dns.start(53, "*", WiFi.softAPIP());
  apActive = true;
  apStopAt = 0;
  Serial.printf("net: AP %s at %s\n", netApSsid().c_str(), WiFi.softAPIP().toString().c_str());
}

static void stopAp() {
  if (!apActive) {
    return;
  }
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  radioSettings();
  apActive = false;
  Serial.println("net: AP off");
}

static void startMdns() {
  if (mdnsActive) {
    MDNS.end();
  }
  mdnsActive = MDNS.begin(config.name.c_str());
  if (mdnsActive) {
    MDNS.addService("http", "tcp", 80);
  }
}

static void onConnected() {
  setState(NET_CONNECTED);
  retrying = false;
  startMdns();
  Serial.printf("net: joined %s as %s, http://%s.local/\n", config.ssid.c_str(),
                WiFi.localIP().toString().c_str(), config.name.c_str());
  if (apActive) {
    // A phone on the setup page polls for the new address; give it time.
    apStopAt = WiFi.softAPgetStationNum() > 0 ? millis() + AP_GRACE_MS : millis();
  }
}

void netBegin() {
  WiFi.persistent(false);  // credentials live in our own NVS namespace
  WiFi.setHostname(config.name.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  radioSettings();
  if (config.ssid.isEmpty()) {
    startAp();
    setState(NET_SETUP);
  } else {
    WiFi.begin(config.ssid.c_str(), config.pass.c_str());
    setState(NET_CONNECTING);
  }
}

void netService() {
  if (apActive) {
    dns.processNextRequest();
  }
  bool connected = WiFi.status() == WL_CONNECTED;
  uint32_t now = millis();

  switch (state) {
    case NET_SETUP:
      break;
    case NET_CONNECTING:
      if (connected) {
        onConnected();
      } else if (now - stateSince > CONNECT_TIMEOUT_MS) {
        // Stop the driver's own retries; they hop channels and break the AP.
        WiFi.disconnect();
        startAp();
        retrying = false;
        retryAt = now + RETRY_INTERVAL_MS;
        setState(NET_FALLBACK);
      }
      break;
    case NET_CONNECTED:
      if (!connected) {
        mdnsActive = false;
        MDNS.end();
        setState(NET_CONNECTING);
      } else if (apActive && (int32_t)(now - apStopAt) >= 0) {
        stopAp();
      }
      break;
    case NET_FALLBACK:
      if (connected) {
        onConnected();
      } else if (retrying && (int32_t)(now - retryAt) >= (int32_t)RETRY_WINDOW_MS) {
        WiFi.disconnect();
        retrying = false;
        retryAt = now + RETRY_INTERVAL_MS;
      } else if (!retrying && (int32_t)(now - retryAt) >= 0 && WiFi.softAPgetStationNum() == 0) {
        // Only retry while nobody is on the setup page.
        WiFi.begin(config.ssid.c_str(), config.pass.c_str());
        retrying = true;
        retryAt = now;
      }
      break;
  }
}

void netConnect(const String &ssid, const String &pass) {
  configSaveWifi(ssid, pass);
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
  retrying = false;
  setState(NET_CONNECTING);
}

void netForget() {
  configSaveWifi("", "");
  if (mdnsActive) {
    MDNS.end();
    mdnsActive = false;
  }
  WiFi.disconnect();
  startAp();
  setState(NET_SETUP);
}

void netRename() {
  WiFi.setHostname(config.name.c_str());
  if (state == NET_CONNECTED) {
    startMdns();
  }
}

NetState netState() {
  return state;
}

const char *netStateName() {
  static const char *const NAMES[] = {"setup", "connecting", "connected", "fallback"};
  return NAMES[state];
}

bool netApActive() {
  return apActive;
}

String netScanJson() {
  int16_t n = WiFi.scanComplete();
  if (n >= 0) {
    // Strongest entry per SSID, strongest first, hidden networks skipped.
    int order[64];
    int count = 0;
    for (int i = 0; i < n && count < 64; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.isEmpty()) {
        continue;
      }
      int dup = -1;
      for (int j = 0; j < count; j++) {
        if (WiFi.SSID(order[j]) == ssid) {
          dup = j;
          break;
        }
      }
      if (dup >= 0) {
        if (WiFi.RSSI(i) > WiFi.RSSI(order[dup])) {
          order[dup] = i;
        }
        continue;
      }
      order[count++] = i;
    }
    for (int a = 1; a < count; a++) {
      for (int b = a; b > 0 && WiFi.RSSI(order[b]) > WiFi.RSSI(order[b - 1]); b--) {
        int t = order[b];
        order[b] = order[b - 1];
        order[b - 1] = t;
      }
    }
    String json = "[";
    for (int k = 0; k < count && k < 25; k++) {
      int i = order[k];
      if (k) {
        json += ',';
      }
      json += "{\"ssid\":" + jsonString(WiFi.SSID(i)) + ",\"rssi\":" + String(WiFi.RSSI(i)) +
              ",\"channel\":" + String(WiFi.channel(i)) +
              ",\"secure\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
    }
    json += "]";
    WiFi.scanDelete();
    scanCache = json;
    scanCachedAt = millis();
  }
  bool stale = scanCache.isEmpty() || millis() - scanCachedAt > SCAN_STALE_MS;
  if (stale && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    WiFi.scanNetworks(true);
  }
  return scanCache.isEmpty() ? String("null") : scanCache;
}
