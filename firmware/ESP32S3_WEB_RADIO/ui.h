#pragma once
/* Touchscreen UI — "circular instrument" design for the ROUND 466x466
 * AMOLED (diameter 466, centre 233,233). Everything is concentric:
 *
 *   MAIN (now playing)
 *     - status line (state · volume · WiFi) at the top
 *     - hero disc: station initial on a dark disc with a colored ring
 *     - volume ring: 270° arc around the disc (drag to set volume)
 *     - status ring: thin outer arc = buffer level, animated while
 *       connecting/reconnecting
 *     - station name, genre, metadata (2 lines / marquee)
 *     - three circular buttons: stations | play/stop | mute
 *     - swipe up/down on the main screen = volume +/-
 *
 *   STATION_LIST
 *     - title, circular back button, 4 rounded station cards,
 *       web hint + scroll indicator
 */

#include <Arduino.h>
#include "display.h"
#include "audio.h"
#include "touch.h"
#include "station_manager.h"
#include "wifi_manager.h"

enum class Screen : uint8_t {
  MAIN,
  STATION_LIST,
};

class UI {
public:
  UI(DisplayUI* display, StreamPlayer* player, StationManager* stations);

  void begin();
  void handleTouch(Touch& t);   // every loop
  void update();                // every loop
  void requestRedraw() { _dirty = true; }
  Screen getScreen() { return _screen; }

private:
  // drawing
  void drawScreen();
  void drawFrame();
  void drawMain();
  void drawStationList();
  void drawStatusLine();
  void drawDisc();
  void drawVolumeRing();
  void drawStatusRing();
  void drawBottomButtons();
  void drawMetadataArea();
  void drawScrollingText(const char* text, int y, uint16_t color);
  void drawCenteredText(const char* text, const GFXfont* f,
                        uint16_t ascent, uint16_t color, int yTop);
  uint16_t paletteColor(uint8_t index);

  // touch
  void handleTap(uint16_t x, uint16_t y);
  void handleSwipe(int16_t dy);
  void setVolumeFromXY(uint16_t x, uint16_t y);
  void selectStation(uint8_t index);
  void togglePlayStop();
  void toggleMute();

  DisplayUI* _d;
  StreamPlayer* _p;
  StationManager* _s;

  Screen _screen = Screen::MAIN;
  uint8_t _listOffset = 0;
  bool _dirty = true;
  bool _wasTouched = false;
  bool _dragging = false;
  bool _dragVolume = false;
  uint16_t _pressX = 0, _pressY = 0;
  uint16_t _curX = 0, _curY = 0;

  uint32_t _lastFullDraw = 0;
  uint32_t _lastMetaDraw = 0;
  uint32_t _lastSig = 0;
  bool _marqueeActive = false;

  static const uint16_t COL_BG      = 0x0000;
  static const uint16_t COL_WHITE   = 0xFFFF;
  static const uint16_t COL_GRAY    = 0x9C63;
  static const uint16_t COL_LGRAY   = 0xBDF7;
  static const uint16_t COL_DARK    = 0x2104; // button fill
  static const uint16_t COL_DISC    = 0x1082; // disc fill
  static const uint16_t COL_TRACK   = 0x39E7; // volume ring track
  static const uint16_t COL_FRAME   = 0x1082;
  static const uint16_t COL_ORANGE  = 0xFD20;
  static const uint16_t COL_GREEN   = 0x07E0;
  static const uint16_t COL_YELLOW  = 0xFFE0;
  static const uint16_t COL_RED     = 0xF800;
  static const uint16_t COL_DIMBLUE = 0x0834;
};
