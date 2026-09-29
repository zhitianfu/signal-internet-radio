#pragma once
/* Unified WLAN client for the Signal UI — one ESP32 station interface:
 * power on/off, async scan, join (password from keyboard), disconnect
 * (keeps credentials), forget (erases credentials), saved-network store.
 *
 * Grounded in the ESP-IDF WLAN model (esp_wifi_scan_start / esp_wifi_connect
 * semantics, credentials in NVS), surfaced through Arduino WiFi.
 *
 * Credentials: NVS Preferences (PREF_WIFI namespace, same keys as the old
 * wifi_manager) plus a JSON map "creds" {"ssid":"pass",...} so every saved
 * network keeps its own password — the prototype's `saved` list semantics. */

#include <Arduino.h>
#include <vector>

struct WcNet {
  String ssid;
  int32_t rssi = 0;
  uint8_t lvl = 1;     // 1..3 (bars)
  bool open = false;   // no encryption
  bool five = false;   // 5 GHz — unsupported (ESP32-S3 is 2.4 only)
  bool hot = false;    // phone-hotspot heuristic (matches prototype tags)
  bool saved = false;  // has stored credentials
};

enum WcState : uint8_t {
  WC_OFF, WC_IDLE, WC_CONNECTING, WC_CONNECTED, WC_FAILED
};

void        wcBegin();                     // load prefs (power + creds)
void        wcLoop();                      // drive connect/scan state machine
void        wcSetPower(bool on);           // WiFi on/off, persisted
bool        wcPowered();
WcState     wcState();
bool        wcConnecting();
bool        wcConnected();
String      wcSsid();                      // current or last SSID
const char* wcStateText();                 // UI subtitle fragment

void        wcScanAsync();                 // start async scan
bool        wcScanReady();                 // true exactly once per scan
std::vector<WcNet>& wcResults();           // last scan results (UI-sorted)

void        wcJoinAsync(const String& ssid, const String& pass); // no save yet
void        wcJoinSaved(const String& ssid);                     // from store
void        wcDisconnect();                // drop link, keep credentials
void        wcForget(const String& ssid);  // erase credentials (+ link)

std::vector<String> wcSavedList();         // saved SSIDs
bool        wcHasSaved(const String& ssid);
bool        wcHasAnyCreds();
String      wcPassFor(const String& ssid);
bool        wcJoinOk();                    // one-shot: last join succeeded
bool        wcJoinFailed();                // one-shot: last join failed
void        wcClearJoins();
