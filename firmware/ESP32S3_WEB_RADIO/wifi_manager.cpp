#include "wifi_manager.h"
#include "config.h"
#include "debug.h"
#include <WiFi.h>
#include <Preferences.h>
#include <DNSServer.h>

#define PREF_WIFI "wifi"
#define PREF_SSID "ssid"
#define PREF_PASS "pass"

static WiFiState _state = WiFiState::DISCONNECTED;
static DNSServer dnsServer;

static void loadSavedCredentials(String& ssid, String& pass) {
  Preferences prefs;
  if (prefs.begin(PREF_WIFI, true)) {
    ssid = prefs.getString(PREF_SSID, "");
    pass = prefs.getString(PREF_PASS, "");
    prefs.end();
  }
}

bool wifiConnect(uint32_t timeoutMs) {
  String ssid, pass;
  loadSavedCredentials(ssid, pass);
  if (ssid.length() == 0) {
    ssid = WIFI_SSID;                  // config.h fallback
    pass = WIFI_PASSWORD;
  }

  _state = WiFiState::CONNECTING;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                // keep the stream stable
  WiFi.begin(ssid.c_str(), pass.c_str());

  LOG_I("wifi", "connecting to \"%s\" ...", ssid.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    _state = WiFiState::STA_CONNECTED;
    LOG_I("wifi", "connected — IP %s, RSSI %d dBm",
          WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  _state = WiFiState::FAILED;
  LOG_E("wifi", "failed to connect within %u ms", timeoutMs);
  return false;
}

bool wifiStartAP() {
  _state = WiFiState::AP_MODE;
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS);
  if (!ok) {
    LOG_E("wifi", "softAP failed");
    return false;
  }
  delay(300);
  dnsServer.start(53, "*", WiFi.softAPIP());   // captive portal DNS
  LOG_I("wifi", "AP \"%s\" up — config at http://%s",
        AP_SSID, WiFi.softAPIP().toString().c_str());
  return true;
}

void wifiSaveCredentials(const char* ssid, const char* pass) {
  Preferences prefs;
  if (prefs.begin(PREF_WIFI, false)) {
    prefs.putString(PREF_SSID, ssid ? ssid : "");
    prefs.putString(PREF_PASS, pass ? pass : "");
    prefs.end();
    LOG_I("wifi", "credentials saved for \"%s\"", ssid ? ssid : "");
  }
}

void wifiLoop() {
  if (_state == WiFiState::AP_MODE) {
    dnsServer.processNextRequest();
  }
}

WiFiState wifiGetState() { return _state; }

const char* wifiStateName() {
  switch (_state) {
    case WiFiState::DISCONNECTED: return "DISCONNECTED";
    case WiFiState::CONNECTING:   return "CONNECTING";
    case WiFiState::STA_CONNECTED:return "STA";
    case WiFiState::AP_MODE:      return "AP";
    case WiFiState::FAILED:       return "FAILED";
  }
  return "?";
}

String wifiGetIP() {
  if (_state == WiFiState::AP_MODE) return WiFi.softAPIP().toString();
  return WiFi.localIP().toString();
}
