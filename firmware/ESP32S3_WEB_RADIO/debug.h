#pragma once
/* Serial logging helpers.
 *
 * IMPORTANT: these are NON-BLOCKING. The USB-CDC (USB-Serial-JTAG) TX
 * FIFO fills up when no host is reading the port, and a raw
 * Serial.printf would then block the calling task — including the
 * audio task, which causes audible stutter. So log lines are skipped
 * when the TX buffer is congested (they're only debug aid anyway).
 */

#include <Arduino.h>

#define LOG_I(tag, fmt, ...) do { \
  if (Serial.availableForWrite() > 48) Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); \
} while (0)
#define LOG_E(tag, fmt, ...) do { \
  if (Serial.availableForWrite() > 48) Serial.printf("[%s][ERROR] " fmt "\n", tag, ##__VA_ARGS__); \
} while (0)
#define LOG_W(tag, fmt, ...) do { \
  if (Serial.availableForWrite() > 48) Serial.printf("[%s][WARN] " fmt "\n", tag, ##__VA_ARGS__); \
} while (0)
