#pragma once
/* CST9217 capacitive touch driver for the Waveshare ESP32-S3-Touch-
 * AMOLED-1.75C. Protocol copied from Waveshare's official BSP
 * (waveshare__esp_lcd_touch_cst9217, esp_lcd_touch_cst9217.c).
 *
 * The 1.75C uses CST9217 (NOT CST816S — that is the 1.8" board):
 *   - I2C: shared SDA=15/SCL=14 bus; controller ACKs at 7-bit 0x5A
 *     (verified by live bus scan; 0x2D does NOT answer — the historical
 *     "0x2D 7-bit / 0x5A 8-bit" note is an inverted conversion)
 *   - 16-bit register addresses, big-endian
 *   - Touch data block at 0xD000, 15 bytes (per SensorLib
 *     TouchDrvCST92xx::getPoint — the reference implementation):
 *       data[0..3] : point 0 (status nibble 0x06=pressed, x, y packed)
 *       data[4]    : palm/gesture high nibble
 *       data[5]    : point count (bit7 = button)
 *       data[6]    : 0xAB device marker
 *       data[7..11]: point 1 (offset i*5 + 2 for i>0)
 *   - After EVERY read the host must write 0xAB back to 0xD000;
 *     without that ACK the controller stalls and sends no new frames.
 *   - Coordinates are 12-bit packed: x = d1<<4 | d3>>4, y = d2<<4 | d3&0xF.
 */

#include <Arduino.h>

enum TouchGesture : uint8_t {
  GESTURE_NONE        = 0x00,
  GESTURE_SINGLE_TAP  = 0x01,   // software-detected for now
  GESTURE_DOUBLE_TAP  = 0x02,   // Prompt 3: real gesture detection
  GESTURE_SWIPE_UP    = 0x03,
  GESTURE_SWIPE_DOWN  = 0x04,
  GESTURE_SWIPE_LEFT  = 0x05,
  GESTURE_SWIPE_RIGHT = 0x06,
  GESTURE_LONG_PRESS  = 0x07,
};

class Touch {
public:
  /* Init: reset pulse (GPIO2, shared with LCD reset), start I2C,
   * probe 0x2D, read chip config (checkcode/resolution/project id).
   * Returns true if the controller answered. Never blocks long. */
  bool begin(int sda, int scl, int rst, int irq);

  /* Poll the controller (call every loop iteration, ~1ms). */
  void loop();

  bool      isPresent() const  { return _present; }
  bool      isTouched() const  { return _downNow; }  // finger down now (live, task-owned)
  uint16_t  getX() const       { return _lx; }       // LIVE x while held (task refreshes each 10 ms)
  uint16_t  getY() const       { return _ly; }       // _x/_y were dead: Touch::loop() has no callers
  /* console 'G x1,y1,x2,y2': inject down + interpolated moves + up so drag
   * gestures (dial, swipe, list scroll, brightness) are testable unplugged */
  void injectDrag(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                  uint8_t steps);
  TouchGesture getGesture() const { return _gesture; }
  void      clearGesture()     { _gesture = GESTURE_NONE; }

  /* Init/scan report as a string (rescans the bus on every call). */
  const char* diag();
  /* Console 'M': stream raw 15-byte reports for ms (tap test). */
  void monitor(uint32_t ms);

  /* High-rate poll task (5 ms) + edge-event queue: taps are never lost
   * to slow renders. popEvent() returns 0=down / 1=up with panel coords. */
  void startTask();
  bool popEvent(uint8_t& type, uint16_t& x, uint16_t& y);
  /* console 'E x,y': inject a full tap (down+up) through the normal
   * event queue — drives the real hit-testing without a finger */
  void injectTap(uint16_t x, uint16_t y);
  /* Live touch marker for the on-screen crosshair (fresh within 700 ms). */
  bool markerActive() const;
  bool markerPos(uint16_t& x, uint16_t& y) const;

private:
  bool readPoint(uint16_t& x, uint16_t& y);
  bool readConfig();
  static void taskFn(void* arg);     // 5 ms poll task (owns Wire)
  void* _task = nullptr;             // TaskHandle_t

  int _sda, _scl, _rst, _irq;
  uint8_t _addr = 0x2D;                 // resolved I2C address
  char _diag[320] = {0};
  bool _present = false;
  bool _touched = false;
  uint16_t _x = 0, _y = 0;
  TouchGesture _gesture = GESTURE_NONE;
  uint32_t _pressStart = 0;    // ms when the current press began
  bool _wasTouched = false;

  void* _queue = nullptr;      // FreeRTOS queue of touch events
  volatile uint16_t _lx = 0, _ly = 0;   // live coords from the task
  volatile uint32_t _lt = 0;            // last activity (ms)
  bool _raw = false;               // debounced task state
  uint8_t _rawN = 0;               // consecutive polls at _raw
  volatile bool _downNow = false;  // live down state published by the task
};

/* Global instance (defined in touch.cpp) — sketch calls touch.begin() etc. */
extern Touch touch;

/* Resolved I2C bus pins: defaults to config values, auto-swapped at boot
 * if the bus scan proves the configured pin order dead. audio.cpp must
 * use these (the codec shares this bus). */
extern int gI2cSda, gI2cScl;
