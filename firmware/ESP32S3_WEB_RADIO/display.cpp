#include "display.h"
#include "config.h"
#include "debug.h"
#include <time.h>

/* CO5300 466x466 QSPI AMOLED via Arduino_GFX (Waveshare 1.75C). */
static Arduino_DataBus* bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
static Arduino_CO5300* panel = new Arduino_CO5300(
  bus, LCD_RESET, 0 /* rotation */, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

DisplayUI::DisplayUI() : gfx(panel) {}

bool DisplayUI::begin() {
  if (!gfx->begin()) {
    LOG_E("display", "gfx->begin() failed");
    return false;
  }
  gfx->fillScreen(RGB565_BLACK);
  panel->setBrightness(255);
  LOG_I("display", "CO5300 %dx%d ready", LCD_WIDTH, LCD_HEIGHT);
  return true;
}

void DisplayUI::setBrightness(uint8_t b) {
  panel->setBrightness(b);
}

void DisplayUI::clear() {
  gfx->fillScreen(RGB565_BLACK);
}

String DisplayUI::getCurrentTime() {
  /* NEVER use getLocalTime() here — its default wait is 5000ms and it
   * blocked EVERY frame for exactly 5s when NTP hadn't synced (no wifi).
   * Non-blocking: format from time() directly, --:-- until first sync. */
  struct tm timeinfo;
  time_t now = time(nullptr);
  if (now < 1600000000LL || !localtime_r(&now, &timeinfo)) {
    return "--:--";
  }
  char buf[6];
  strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
  return String(buf);
}
