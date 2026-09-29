#pragma once
/* Web configuration server (Prompt 4): serves a management page for
 * stations (view/add/edit/delete) and WiFi credentials. Runs in STA
 * mode (LAN) and in AP mode (192.168.4.1 captive portal).
 *
 * API:
 *   GET  /api/stations          -> JSON array
 *   GET  /api/status            -> {mode, ip, rssi, station, state, volume}
 *   POST /api/add               name, url, genre
 *   POST /api/update            id, name, url, genre
 *   POST /api/delete            id
 *   POST /api/wifi              ssid, pass  (saves + restarts)
 */

#include <Arduino.h>

void webInit();
void webLoop();   // call every loop (also services captive-portal DNS)
