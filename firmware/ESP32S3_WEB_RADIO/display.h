#pragma once
/* Display module: CO5300 466x466 QSPI AMOLED via Arduino_GFX.
 * Low-level access only — screens/UI live in ui.cpp. */

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

class DisplayUI {
public:
  DisplayUI();

  /* Init the panel + backlight. Returns true on success. */
  bool begin();

  void clear();

  /* Direct access to the GFX object for drawing primitives (ui.cpp). */
  Arduino_GFX* getGfx() { return gfx; }

  /* Local time "HH:MM" (CST). */
  String getCurrentTime();

  /* AMOLED brightness 0..255 (CO5300 panel register). */
  void setBrightness(uint8_t b);

private:
  Arduino_GFX* gfx = nullptr;
};
