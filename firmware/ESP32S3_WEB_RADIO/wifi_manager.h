#pragma once
/* WiFi manager (Prompt 4): STA connect with credentials stored in NVS,
 * plus AP-mode fallback with a captive portal for first-time setup.
 *
 * Boot flow (see main): wifiConnect() tries saved credentials (NVS),
 * then the config.h defaults. If that fails, wifiStartAP() brings up
 * the "ESP32-Radio-Setup" hotspot; any browser request is redirected
 * to the config page where the user enters their WiFi credentials.
 * After saving, the device restarts and reconnects as a station.
 */

#include <Arduino.h>

enum class WiFiState : uint8_t {
  DISCONNECTED,
  CONNECTING,
  STA_CONNECTED,
  AP_MODE,
  FAILED,
};

/* Connect to STA using saved NVS credentials (fallback: config.h).
 * Blocks up to timeoutMs. Returns true on success. */
bool wifiConnect(uint32_t timeoutMs);

/* Start the setup hotspot + captive portal. */
bool wifiStartAP();

/* Store new credentials in NVS (caller triggers the restart). */
void wifiSaveCredentials(const char* ssid, const char* pass);

/* Service the captive-portal DNS while in AP mode (call every loop). */
void wifiLoop();

WiFiState wifiGetState();
const char* wifiStateName();
inline bool wifiIsAP() { return wifiGetState() == WiFiState::AP_MODE; }
String wifiGetIP();     // LAN IP in STA mode, softAP IP in AP mode
