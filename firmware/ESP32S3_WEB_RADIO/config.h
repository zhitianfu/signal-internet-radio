#pragma once

/* ============================================================
 * ESP32-S3 Internet Radio — central configuration
 * Board: Waveshare ESP32-S3-Touch-AMOLED-1.75C
 *   - CO5300 466x466 AMOLED, QSPI (not SSD1351!)
 *   - CST816S capacitive touch, I2C
 *   - ES8311 audio codec + PA (built-in); mics via ES7210
 * Edit pins / WiFi / defaults here. All modules include this.
 * ============================================================ */

// ---------------- Display: CO5300 466x466 AMOLED (QSPI) ------
#define LCD_CS      12
#define LCD_SCLK    38
#define LCD_SDIO0   4
#define LCD_SDIO1   5
#define LCD_SDIO2   6
#define LCD_SDIO3   7
#define LCD_RESET   1
#define LCD_WIDTH   466
#define LCD_HEIGHT  466

// ---------------- Touch: CST9217 (I2C, shared with ES8311) -----
// NOTE: the 1.75C uses CST9217 at 0x2D (not CST816S at 0x15!).
#define TOUCH_SDA     15
#define TOUCH_SCL     14
#define TOUCH_INT     11   // interrupt pin (optional; driver polls)
#define TOUCH_RST     2    // reset pin (shared with LCD reset on this board)
#define TOUCH_I2C_ADDR 0x2D

// ---------------- Audio: ES8311 codec + PA --------------------
// I2S clock pins are shared by the ES8311 (speaker/mic) and the
// ES7210 (dual-mic ADC) on this board.
#define PIN_ES7210_BCLK  9
#define PIN_ES7210_LRCK  45
#define PIN_ES7210_DIN   10
#define PIN_ES7210_MCLK  16
#define PIN_ES8311_DOUT  8
#ifndef PIN_ES8311_MCLK
#define PIN_ES8311_MCLK  PIN_ES7210_MCLK
#endif
#define PA  46            // power-amplifier enable

// ---------------- Power management (AXP2101 PMIC) -------------
#define XPOWERS_CHIP_AXP2101   // used by XPowersLib if battery code is added later

// ---------------- WiFi ----------------------------------------
#define WIFI_SSID      ""   // optional fallback only; real Wi-Fi is entered in the UI and stored in NVS
#define WIFI_PASSWORD  ""
#define WIFI_TIMEOUT_MS 20000   // STA connect timeout before AP fallback
#define AP_SSID        ""   // unused: device is STA-only, never an AP
#define AP_PASS        ""

// ---------------- Controls -------------------------------------
#define BTN_NEXT 0   // BOOT button (GPIO0): next station fallback

// ---------------- Touch calibration (Prompt 3) ------------------
// Flip these if the touch axes don't line up with the display axes.
#define TOUCH_SWAP_XY   0
#define TOUCH_MIRROR_X  0
#define TOUCH_MIRROR_Y  0

// ---------------- Radio defaults ------------------------------
#define DEFAULT_VOLUME 85             // 0..100 (software volume)
#define UI_UPDATE_INTERVAL_MS 1000    // now-playing screen refresh (1 s)

// ---------------- Audio streaming (Prompt 2) ------------------
#define RECONNECT_BASE_DELAY_MS 1000    // first reconnect attempt delay
#define RECONNECT_MAX_DELAY_MS 60000    // backoff cap
#define STREAM_CONNECT_TIMEOUT_MS 3000  // plain HTTP connect timeout
#define STREAM_CONNECT_TIMEOUT_SSL_MS 8000 // TLS connect timeout
