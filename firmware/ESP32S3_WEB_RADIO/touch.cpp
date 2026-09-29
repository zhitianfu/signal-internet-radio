#include "touch.h"
#include "config.h"
#include "debug.h"
#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

struct TouchEv { uint8_t type; uint16_t x, y; };   // 0=down 1=up

// CST9217 register map (from esp_lcd_touch_cst9217.c)
#define CST9217_DATA_REG       0xD000
#define CST9217_CMD_MODE_REG   0xD101
#define CST9217_CHECKCODE_REG  0xD1FC
#define CST9217_RESOLUTION_REG 0xD1F8
#define CST9217_PROJECT_ID_REG 0xD204
#define CST9217_ACK_VALUE      0xAB
#define CST9217_MAX_TOUCH_POINTS 2

int gI2cSda = -1, gI2cScl = -1;        // resolved pins (see touch.h)
static uint8_t s_addr = TOUCH_I2C_ADDR; // resolved address (see begin())

/* One bus, two users (poll task + console). Mutex per transaction —
 * NEVER suspend the task mid-transaction (that wedged the bus once and
 * froze the whole UI inside Wire). */
static SemaphoreHandle_t s_bus = nullptr;
static inline void busLock()   { if (s_bus) xSemaphoreTake(s_bus, portMAX_DELAY); }
static inline void busUnlock() { if (s_bus) xSemaphoreGive(s_bus); }

static bool regWrite(uint16_t reg, const uint8_t* data, size_t len) {
  busLock();
  Wire.beginTransmission(s_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (len) Wire.write(data, len);
  bool ok = Wire.endTransmission() == 0;
  busUnlock();
  return ok;
}

static bool regRead(uint16_t reg, uint8_t* buf, size_t len) {
  busLock();
  Wire.beginTransmission(s_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  bool ok = true;
  if (Wire.endTransmission(false) != 0) {          // repeated start
    ok = false;
  } else {
    delay(1);                                      // sensor settle
    if (Wire.requestFrom((uint8_t)s_addr, (uint8_t)len) != (int)len) {
      ok = false;
    } else {
      for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    }
  }
  busUnlock();
  return ok;
}

/* Probe every 7-bit address; return count, fill responders. */
static int i2cScan(uint8_t* found, int maxN) {
  busLock();
  int n = 0;
  for (uint16_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission((uint8_t)a);
    if (Wire.endTransmission() == 0 && n < maxN) found[n++] = (uint8_t)a;
  }
  busUnlock();
  return n;
}

static bool i2cProbe(uint8_t addr) {
  busLock();
  Wire.beginTransmission(addr);
  bool ok = Wire.endTransmission() == 0;
  busUnlock();
  return ok;
}

const char* Touch::diag() {
  uint8_t f[8];
  int n = i2cScan(f, 8);
  int off = snprintf(_diag, sizeof(_diag),
                     "sda=%d scl=%d addr=0x%02X present=%d scan(%d):",
                     _sda, _scl, _addr, _present, n);
  for (int i = 0; i < n && off < (int)sizeof(_diag) - 8; i++)
    off += snprintf(_diag + off, sizeof(_diag) - off, " 0x%02X", f[i]);
  off += snprintf(_diag + off, sizeof(_diag) - off, " probe=%d",
                  i2cProbe(_addr) ? 1 : 0);
  /* panel resolution from the config block (0xD1F8) — if this is not
   * 466x466 the raw coordinates need scaling before the mirror */
  uint8_t rd[4];
  if (regRead(0xD1F8, rd, 4))
    snprintf(_diag + off, sizeof(_diag) - off, " res=%u,%u",
             (unsigned)((rd[1] << 8) | rd[0]), (unsigned)((rd[3] << 8) | rd[2]));
  return _diag;
}

/* ---------------- high-rate poll task ----------------
 * One read every 5 ms; edges go into a queue so a tap can never be lost
 * between two slow full-screen renders. The task exclusively owns the
 * I2C bus after startTask(); monitor()/diag() suspend it while probing. */
void Touch::taskFn(void* arg) {
  Touch* s = (Touch*)arg;
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    /* 10 ms period, loop priority (set at create): work per cycle is
     * ~4 ms, so idle/loop always get their slice. A 5 ms period at
     * higher priority starved the UI core — screen froze, console died. */
    vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    if (!s->_present || !s->_queue) continue;
    uint16_t x = 0, y = 0;
    bool raw = s->readPoint(x, y);
    /* debounce: the chip sometimes emits stray release/idle frames in
     * the middle of a hold — require 2 consecutive agreeing polls
     * before believing a state change (filters single-frame glitches
     * that produced duplicate down/up edges) */
    if (raw == s->_raw) {
      if (s->_rawN < 100) s->_rawN++;
    } else {
      s->_raw = raw;
      s->_rawN = 1;
    }
    bool down = (s->_rawN >= 2) ? s->_raw : s->_wasTouched;
    s->_downNow = down;   /* live state for isTouched() — Touch::loop() never runs */
    TouchEv ev;
    /* only refresh live coords when the read ACTUALLY returned a touch —
     * during the release-hold window down() is still true but readPoint
     * left x,y at 0, which used to wipe the last press point and made
     * every tap's "moved" look like 300px (all dispatch gates failed) */
    if (raw && down) { s->_lx = x; s->_ly = y; s->_lt = millis(); }
    if (down && !s->_wasTouched) {
      ev = {0, x, y};
      if (xQueueSend((QueueHandle_t)s->_queue, &ev, 0) != pdTRUE) {
        TouchEv drop; xQueueReceive((QueueHandle_t)s->_queue, &drop, 0);
        xQueueSend((QueueHandle_t)s->_queue, &ev, 0);
      }
    } else if (!down && s->_wasTouched) {
      ev = {1, s->_lx, s->_ly};
      if (xQueueSend((QueueHandle_t)s->_queue, &ev, 0) != pdTRUE) {
        TouchEv drop; xQueueReceive((QueueHandle_t)s->_queue, &drop, 0);
        xQueueSend((QueueHandle_t)s->_queue, &ev, 0);
      }
    }
    s->_wasTouched = down;
  }
}

void Touch::startTask() {
  if (_task) return;
  _queue = xQueueCreate(16, sizeof(TouchEv));
  TaskHandle_t h = nullptr;
  xTaskCreatePinnedToCore(taskFn, "touch", 4096, this, 1, &h, 1);
  _task = h;
  Serial.printf("[ui] touch task: 10ms period, loop prio, core1\n");
}

void Touch::injectTap(uint16_t x, uint16_t y) {
  if (!_queue) return;
  QueueHandle_t q = (QueueHandle_t)_queue;
  TouchEv e;
  e.type = 0; e.x = x; e.y = y;
  xQueueSend(q, &e, 0);
  e.type = 1;
  xQueueSend(q, &e, 0);
}

void Touch::injectDrag(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                       uint8_t steps) {
  if (!_queue || steps == 0) return;
  QueueHandle_t q = (QueueHandle_t)_queue;
  TouchEv e;
  e.type = 0; e.x = x1; e.y = y1;
  xQueueSend(q, &e, 0);
  for (uint8_t i = 1; i <= steps && i < 13; i++) {
    e.type = 2;
    e.x = (uint16_t)(x1 + (int32_t)(x2 - x1) * i / steps);
    e.y = (uint16_t)(y1 + (int32_t)(y2 - y1) * i / steps);
    xQueueSend(q, &e, 0);
  }
  e.type = 1; e.x = x2; e.y = y2;
  xQueueSend(q, &e, 0);
}

bool Touch::popEvent(uint8_t& type, uint16_t& x, uint16_t& y) {
  if (!_queue) return false;
  TouchEv e;
  if (xQueueReceive((QueueHandle_t)_queue, &e, 0) != pdTRUE) return false;
  type = e.type; x = e.x; y = e.y;
  return true;
}

bool Touch::markerActive() const { return _lt && (millis() - _lt) < 300; }
bool Touch::markerPos(uint16_t& x, uint16_t& y) const {
  x = _lx; y = _ly; return _lt != 0;
}

bool Touch::begin(int sda, int scl, int rst, int irq) {
  _sda = sda; _scl = scl; _rst = rst; _irq = irq;
  if (_irq >= 0) pinMode(_irq, INPUT);

  // Reset pulse (same as the official BSP: 10ms low, 50ms high).
  // NOTE: on this board GPIO2 is shared with the LCD reset line.
  if (_rst >= 0) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, LOW);
    delay(10);
    digitalWrite(_rst, HIGH);
    delay(50);
  }

  // Bring up the bus and find who answers (codec lives here too).
  uint8_t found[8];
  Wire.begin(_sda, _scl);
  Wire.setTimeOut(50);                 // a wedged bus errors out, never hangs
  if (!s_bus) s_bus = xSemaphoreCreateMutex();
  delay(5);
  int nf = i2cScan(found, 8);
  int usedSda = _sda, usedScl = _scl;
  if (nf == 0 && _sda != _scl) {
    // Nobody answered: maybe the pin roles are swapped — try the mirror.
    Wire.begin(_scl, _sda);
    delay(5);
    int nf2 = i2cScan(found, 8);
    if (nf2 > 0) {
      nf = nf2; usedSda = _scl; usedScl = _sda;
    } else {
      Wire.begin(_sda, _scl);
      delay(5);
    }
  }
  _sda = usedSda; _scl = usedScl;
  gI2cSda = usedSda; gI2cScl = usedScl;

  // Resolve the touch address. Live scan on the 1.75C showed the
  // controller ACKing 0x5A (the BSP's real 7-bit address — the older
  // "0x2D = 7-bit, 0x5A = 8-bit" note is inverted). Fallbacks cover
  // the CST816S/FT variants; probe-only, no register writes to strangers.
  s_addr = TOUCH_I2C_ADDR; _addr = s_addr;
  if (!i2cProbe(s_addr)) {
    const uint8_t cands[] = {0x5A, 0x15, 0x38};
    for (uint8_t c : cands) {
      if (i2cProbe(c)) { s_addr = c; _addr = c; break; }
    }
  }
  _present = i2cProbe(_addr);

  int off = snprintf(_diag, sizeof(_diag),
                     "boot: sda=%d scl=%d scan=%d [", usedSda, usedScl, nf);
  for (int i = 0; i < nf && off < (int)sizeof(_diag) - 8; i++)
    off += snprintf(_diag + off, sizeof(_diag) - off, "0x%02X ", found[i]);
  snprintf(_diag + off, sizeof(_diag) - off, "] addr=0x%02X", _addr);

  if (!_present) {
    LOG_E("touch", "%s — no controller ACK", _diag);
    return false;
  }

  LOG_I("touch", "CST9217 found at 0x%02X, reading config...", _addr);
  bool cfg = readConfig();
  LOG_I("touch", "config %s", cfg ? "OK" : "unreadable (continuing)");
  return true;
}

bool Touch::readConfig() {
  // Enter command mode, then read chip identity (per BSP)
  uint8_t cmd_mode[2] = {0xD1, 0x01};
  bool ok = regWrite(CST9217_CMD_MODE_REG, cmd_mode, sizeof(cmd_mode));
  if (!ok) { LOG_E("touch", "enter command mode failed"); return false; }
  delay(10);

  uint8_t d[4];
  if (regRead(CST9217_CHECKCODE_REG, d, 4))
    LOG_I("touch", "checkcode 0x%02X%02X%02X%02X", d[0], d[1], d[2], d[3]);
  if (regRead(CST9217_RESOLUTION_REG, d, 4))
    LOG_I("touch", "resolution %ux%u", (d[1] << 8) | d[0], (d[3] << 8) | d[2]);
  if (regRead(CST9217_PROJECT_ID_REG, d, 4))
    LOG_I("touch", "chip type 0x%04X project 0x%04X", (d[3] << 8) | d[2], (d[1] << 8) | d[0]);
  return true;
}

void Touch::loop() {
  if (!_present) return;

  uint16_t x, y;
  bool down = readPoint(x, y);

  if (down && !_wasTouched) {
    _pressStart = millis();          // new press
  }
  if (!down && _wasTouched) {
    // release: classify as tap if it was short
    uint32_t dur = millis() - _pressStart;
    _gesture = (dur < 300) ? GESTURE_SINGLE_TAP : GESTURE_NONE;
  }
  _wasTouched = down;

  if (down) { _x = x; _y = y; }
  _touched = down;
}

bool Touch::readPoint(uint16_t& x, uint16_t& y) {
  // Byte-exact mirror of SensorLib TouchDrvCST92xx::getPoint():
  //   1) read 15 bytes at 0xD000 (2 fingers * 5 + 5 header/footer)
  //   2) WRITE 0xAB back to 0xD000 — mandatory ACK, the controller
  //      stalls (no new frames) until it arrives. This was missing:
  //      reads answered, we never acked, touch appeared dead.
  //   3) byte6 = ACK marker, byte5 = point count, byte0..3 = finger0.
  uint8_t data[15] = {0};
  bool ok = regRead(CST9217_DATA_REG, data, sizeof(data));
  uint8_t ack = CST9217_ACK_VALUE;
  regWrite(CST9217_DATA_REG, &ack, 1);          // ack EVERY read: valid, empty or malformed
  if (!ok) return false;
  if (data[6] != CST9217_ACK_VALUE) return false;

  uint8_t points = data[5] & 0x7F;
  if (points > CST9217_MAX_TOUCH_POINTS) points = CST9217_MAX_TOUCH_POINTS;
  if (points == 0) return false;

  // Point 0 lives in data[0..3]; status nibble must be 0x06 (valid)
  if ((data[0] & 0x0F) != 0x06) return false;
  x = (data[1] << 4) | (data[3] >> 4);
  y = (data[2] << 4) | (data[3] & 0x0F);
  // Board transform — exactly what Waveshare's own LVGL example does
  // (06_LVGL_Widgets.ino): setMaxCoordinates(466,466) + setMirrorXY(true,true)
  // i.e. x = 466 - x_raw, y = 466 - y_raw. Raw CST9217 output is rotated
  // 180° relative to the panel; without this every tap missed its targets.
  x = 466 - x;
  y = 466 - y;
  return true;
}

void Touch::monitor(uint32_t ms) {
  if (!_present) { Serial.println("[M] touch not present"); return; }
  Serial.printf("[M] raw monitor %lu ms — tap the screen now\n", (unsigned long)ms);
  uint32_t t0 = millis(), lastHb = t0;
  uint32_t reads = 0, frames = 0;
  int lastX = -99, lastY = -99;
  bool lastDown = false;
  while (millis() - t0 < ms) {
    uint8_t d[15] = {0};
    bool ok = regRead(CST9217_DATA_REG, d, sizeof(d));
    uint8_t ack = CST9217_ACK_VALUE;
    regWrite(CST9217_DATA_REG, &ack, 1);
    reads++;
    bool marker = ok && d[6] == CST9217_ACK_VALUE;
    uint8_t pts = marker ? (d[5] & 0x7F) : 0;
    bool down = marker && pts >= 1 && pts <= 2 && (d[0] & 0x0F) == 0x06;
    int px = -1, py = -1;
    if (down) {
      px = 466 - (((d[1] << 4) | (d[3] >> 4)));
      py = 466 - (((d[2] << 4) | (d[3] & 0x0F)));
    }
    // print only state/position CHANGES so every contact in the window
    // shows up (a held finger no longer eats the 40-frame cap)
    bool changed = (down != lastDown) || (down && (px != lastX || py != lastY));
    if (ok && marker && changed && frames < 80) {
      frames++;
      Serial.printf("[M] raw %02X %02X %02X %02X %02X %02X%02X %02X ...",
                    d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8]);
      if (down) Serial.printf("  -> press panel x=%d y=%d", px, py);
      else if (!down && lastDown) Serial.printf("  -> release (was %d,%d)", lastX, lastY);
      else if (marker && pts == 0 && lastDown) Serial.printf("  -> idle");
      Serial.write('\n');
    }
    if (down) { lastX = px; lastY = py; }
    lastDown = down;
    if (millis() - lastHb > 1000) {
      Serial.printf("[M] %lus reads=%lu frames=%lu\n",
                    (unsigned long)((millis() - t0) / 1000),
                    (unsigned long)reads, (unsigned long)frames);
      lastHb = millis();
    }
    delay(10);
  }
  Serial.printf("[M] done: %lu reads, %lu marker frames\n",
                (unsigned long)reads, (unsigned long)frames);
}

Touch touch;   // global instance
