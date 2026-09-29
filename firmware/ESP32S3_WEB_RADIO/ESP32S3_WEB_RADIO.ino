/*
 * ESP32-S3 Internet Radio — SIGNAL (Concept D) firmware.
 *
 * Boot flow: display -> touch -> stations -> boot splash (Signal UI) ->
 * audio engine -> async Wi-Fi join -> auto-connect current station.
 *
 * UI: sig_ui / sig_paint / sig_wifi / sig_flow — a faithful port of the
 * 004-signal high-fidelity prototype (layout from its CSS, behavior from
 * its JS). Touch drives the same gestures: drag the dial ring, swipe up
 * for stations, swipe down for settings, settings -> Wi-Fi.
 *
 * Board: Waveshare ESP32-S3-Touch-AMOLED-1.75C
 *   CO5300 466x466 AMOLED (QSPI)         -> display.cpp
 *   CST9217 touch (I2C 0x2D)             -> touch.cpp
 *   ES8311 codec + I2S streaming task    -> audio.cpp
 *   unified WLAN (scan/join/forget/NVS)  -> wifi_client.cpp
 *   station list in LittleFS JSON        -> station_manager.cpp
 *   Signal UI (prototype port)           -> sig_ui.cpp + friends
 *   pins / defaults                      -> config.h
 */
#include <WiFi.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"
#include "debug.h"
#include "display.h"
#include "touch.h"
#include "audio.h"
#include "station_manager.h"
#include "wifi_client.h"
#include "sig_ui.h"

#define BOOT_LOG(fmt, ...) do { LOG_I("boot", fmt, ##__VA_ARGS__); \
                                Serial.flush(); } while (0)

StreamPlayer player(PIN_ES7210_BCLK, PIN_ES7210_LRCK, PIN_ES8311_DOUT);
DisplayUI display;

void initClock() {
  configTzTime("CST-8", "pool.ntp.org", "time.nist.gov");
}

void setup() {
  Serial.begin(115200);
  /* NON-BLOCKING console: with the default tx timeout, every printf spins
   * up to ~1 s once the host-side tty buffer fills (nobody reading), which
   * starves sigui.pump()/audio.loop() -> frozen watchface + silent audio.
   * setTxTimeoutMs(0) drops debug bytes instead of stalling the UI. */
  Serial.setTxTimeoutMs(0);
  delay(200);
  Serial.println("[boot] ESP32-S3 SIGNAL radio");
  Serial.flush();

  /* 1. display — splash appears on the first UI pump */
  BOOT_LOG("init display ...");
  bool ok = display.begin();
  BOOT_LOG("display %s", ok ? "OK" : "FAIL");

  /* 2. touch (CST9217 at 0x2D) */
  BOOT_LOG("init touch ...");
  ok = touch.begin(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
  BOOT_LOG("touch %s", ok ? "OK" : "FAIL");

  /* 3. stations — reseed once with the Signal station set */
  BOOT_LOG("init stations ...");
  {
    Preferences p;
    if (p.begin("sig", false)) {
      if (p.getUChar("seed", 0) != 1) {
        if (LittleFS.begin(true)) LittleFS.remove("/stations.json");
        p.putUChar("seed", 1);
      }
      p.end();
    }
  }
  ok = stationManager.begin();
  BOOT_LOG("stations %s (%u)", ok ? "OK" : "FAIL",
           stationManager.getStationCount());

  /* 4. audio engine */
  BOOT_LOG("init audio ...");
  player.begin();
  BOOT_LOG("audio OK");

  /* 5. Signal UI (loads state, draws boot splash) */
  BOOT_LOG("init UI ...");
  sigui.begin(&display, &player, &stationManager);
  BOOT_LOG("UI OK");

  /* 6. clock + async Wi-Fi (joins the stored network in background) */
  initClock();
  BOOT_LOG("init wifi ...");
  wcBegin();
  BOOT_LOG("wifi %s", wcPowered() ? "on" : "off");

  /* 7. BOOT button (GPIO0): short = next station, long = standby */
  pinMode(BTN_NEXT, INPUT_PULLUP);
  BOOT_LOG("radio ready");
}

void loop() {
  sigui.pump();

  /* BOOT button fallback: short press = next station, >=1 s = standby */
  static bool wasDown = false;
  static uint32_t downT0 = 0, lastTap = 0;
  bool down = digitalRead(BTN_NEXT) == LOW;
  if (down && !wasDown) { downT0 = millis(); wasDown = true; }
  if (!down && wasDown) {
    wasDown = false;
    uint32_t held = millis() - downT0;
    if (held >= 1000) sigui.toggleStandby();
    else if (held > 30 && millis() - lastTap > 300) {
      lastTap = millis();
      sigui.btnNext();
    }
  }
}
