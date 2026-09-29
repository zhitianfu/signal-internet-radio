#include "ui.h"
#include "config.h"
#include "debug.h"
#include "fonts.h"
#include <WiFi.h>
#include <math.h>

// ------------------------------------------------------------------
// Round geometry: circle diameter 466, centre (233, 233).
// ------------------------------------------------------------------
#define CX          233
#define CY          233

// main screen (matches demo A mockup)
#define STATUS_Y    48
#define DISC_CX     233
#define DISC_CY     170
#define DISC_R      76
#define RING_R1     86          // volume ring inner
#define RING_R2     96          // volume ring outer
#define SRING_R     104         // status ring radius
#define STATION_Y   282
#define GENRE_Y     316
#define META_Y      344
#define META_LINE_H 24
#define BTN_Y       416
#define BTN_LX      150         // stations
#define BTN_CX      233         // play/stop
#define BTN_RX      316         // mute
#define BTN_R       34
#define BTN_CR      38          // center button radius

// station list
#define LIST_TITLE_Y 36
#define BACK_CX     75
#define BACK_CY     85
#define BACK_R      28
#define LIST_X      60
#define LIST_W      346
#define LIST_Y0     128
#define LIST_ROW_H  66
#define LIST_ROW_HH 58
#define LIST_VISIBLE 4
#define HINT_Y      400
#define IND_Y       424

// marquee
#define MARQUEE_STEP_MS 35

static const uint16_t PALETTE[9] = {
  0xFD20, 0x07E0, 0x07FF, 0xF81F, 0xFFE0,
  0x001F, 0xF800, 0xFD7F, 0x9C63
};

// ------------------------------------------------------------------
// Font-aware text helpers
// ------------------------------------------------------------------
static uint16_t textWidthPx(const char* text, const GFXfont* f) {
  uint16_t w = 0;
  for (const char* p = text; *p; p++) {
    uint8_t c = (uint8_t)*p;
    if (c >= f->first && c <= f->last) {
      w += pgm_read_byte(&f->glyph[c - f->first].xAdvance);
    }
  }
  return w;
}

// ------------------------------------------------------------------
UI::UI(DisplayUI* display, StreamPlayer* player, StationManager* stations)
  : _d(display), _p(player), _s(stations) {}

void UI::begin() {
  _dirty = true;
  update();
  LOG_I("ui", "UI ready (round %dx%d)", LCD_WIDTH, LCD_HEIGHT);
}

uint16_t UI::paletteColor(uint8_t index) {
  return PALETTE[index % 9];
}

// ------------------------------------------------------------------
// Public per-loop entry points
// ------------------------------------------------------------------
void UI::handleTouch(Touch& t) {
  uint16_t rawX = t.getX(), rawY = t.getY();
#if TOUCH_SWAP_XY
  uint16_t tmp = rawX; rawX = rawY; rawY = tmp;
#endif
#if TOUCH_MIRROR_X
  rawX = LCD_WIDTH - 1 - rawX;
#endif
#if TOUCH_MIRROR_Y
  rawY = LCD_HEIGHT - 1 - rawY;
#endif

  bool touched = t.isTouched();
  if (touched && !_wasTouched) {            // press edge
    _pressX = _curX = rawX;
    _pressY = _curY = rawY;
    _dragging = false;
    _dragVolume = false;
  }

  if (touched) {
    _curX = rawX;
    _curY = rawY;

    // Volume ring drag (main screen): press on the ring around the disc
    if (_screen == Screen::MAIN) {
      float dx = (float)_curX - DISC_CX;
      float dy = (float)_curY - DISC_CY;
      float dist = sqrtf(dx * dx + dy * dy);
      bool inRing = (dist >= 78.0f && dist <= 106.0f);
      if (_dragVolume || inRing) {
        _dragVolume = true;
        _dragging = true;
        setVolumeFromXY(_curX, _curY);      // live partial redraw
      }
    }
  } else if (_wasTouched) {                 // release edge
    if (_dragVolume) {
      _dragVolume = false;
      _dragging = false;
      _dirty = true;
    } else if (!_dragging) {
      int16_t dx = _curX - _pressX;
      int16_t dy = _curY - _pressY;
      if (abs(dx) < 30 && abs(dy) < 30) {
        handleTap(_curX, _curY);
      } else if (abs(dy) > abs(dx)) {
        handleSwipe(dy);                    // vertical swipe
      }
    }
  }
  _wasTouched = touched;
}

static uint32_t contentSig(StationManager* s, StreamPlayer* p) {
  char meta[64];
  p->getMetadata(meta, sizeof(meta));
  uint32_t sum = 0;
  for (int i = 0; meta[i] && i < 40; i++) sum += meta[i];
  int rssi = WiFi.RSSI();
  if (rssi < -100) rssi = -100;
  if (rssi > 0) rssi = 0;
  return s->getCurrentIndex() * 1000000UL +
         (uint32_t)p->getState() * 10000 +
         p->getVolume() * 100 +
         (p->isMuted() ? 50 : 0) +
         (sum % 97) +
         ((uint32_t)((rssi + 100) / 8)) * 3;
}

void UI::update() {
  uint32_t now = millis();
  uint32_t sig = contentSig(_s, _p);
  bool changed = (sig != _lastSig);
  if (_dirty || (changed && now - _lastFullDraw >= 200)) {
    _lastFullDraw = now;
    _lastSig = sig;
    _dirty = false;
    drawScreen();
  } else if (_screen == Screen::MAIN && _marqueeActive &&
             now - _lastMetaDraw >= MARQUEE_STEP_MS) {
    _lastMetaDraw = now;
    drawMetadataArea();
  } else if (_screen == Screen::MAIN && !_marqueeActive &&
             (_p->getState() == StreamState::CONNECTING ||
              _p->getState() == StreamState::RECONNECTING) &&
             now - _lastMetaDraw >= 60) {
    _lastMetaDraw = now;
    drawStatusRing();
  }
}

// ------------------------------------------------------------------
// Drawing
// ------------------------------------------------------------------
void UI::drawScreen() {
  drawFrame();
  if (_screen == Screen::MAIN) drawMain();
  else drawStationList();
}

void UI::drawFrame() {
  Arduino_GFX* g = _d->getGfx();
  g->drawCircle(CX, CY, 225, COL_FRAME);
}

void UI::drawCenteredText(const char* text, const GFXfont* f,
                          uint16_t ascent, uint16_t color, int yTop) {
  Arduino_GFX* g = _d->getGfx();
  int w = textWidthPx(text, f);
  int x = CX - w / 2;
  if (x < 0) x = 0;
  g->setFont(f);
  g->setTextColor(color);
  g->setCursor(x, yTop + ascent);           // yTop -> baseline
  g->print(text);
}

void UI::drawStatusLine() {
  char line[48];
  uint16_t col = COL_GRAY;
  if (wifiIsAP()) {
    snprintf(line, sizeof(line), "AP MODE  -  %s", wifiGetIP().c_str());
    col = COL_YELLOW;
  } else {
    StreamState st = _p->getState();
    switch (st) {
      case StreamState::PLAYING:      col = COL_GREEN;  break;
      case StreamState::CONNECTING:   col = COL_YELLOW; break;
      case StreamState::RECONNECTING: col = COL_ORANGE; break;
      default: break;
    }
    snprintf(line, sizeof(line), "%s  -  %u%%  -  %ddBm",
             _p->stateName(), _p->getVolume(), WiFi.RSSI());
  }
  drawCenteredText(line, &fontMono10, FONT_MONO10_ASCENT, col, STATUS_Y);
}

void UI::drawDisc() {
  Arduino_GFX* g = _d->getGfx();
  uint16_t pal = paletteColor(_s->getCurrentIndex());
  g->fillCircle(DISC_CX, DISC_CY, DISC_R, COL_DISC);
  g->drawCircle(DISC_CX, DISC_CY, DISC_R, pal);
  g->drawCircle(DISC_CX, DISC_CY, DISC_R - 3, pal);
  // station initial
  char ch[2] = { _s->getCurrentStation().name.charAt(0), '\0' };
  if (ch[0]) {
    int w = textWidthPx(ch, &fontSansb44);
    int x = DISC_CX - w / 2;
    int yTop = DISC_CY - FONT_SANSB44_HEIGHT / 2;
    g->setFont(&fontSansb44);
    g->setTextColor(COL_WHITE);
    g->setCursor(x, yTop + FONT_SANSB44_ASCENT);
    g->print(ch);
  }
}

void UI::drawVolumeRing() {
  Arduino_GFX* g = _d->getGfx();
  uint16_t pal = paletteColor(_s->getCurrentIndex());
  uint8_t vol = _p->getVolume();
  // 270 degree arc, gap at the bottom (135..405)
  g->drawArc(DISC_CX, DISC_CY, RING_R2, RING_R1, 135, 405, COL_TRACK);
  if (vol > 0) {
    g->drawArc(DISC_CX, DISC_CY, RING_R2, RING_R1, 135, 135 + vol * 2.7f, pal);
  }
}

void UI::drawStatusRing() {
  Arduino_GFX* g = _d->getGfx();
  StreamState st = _p->getState();
  g->drawArc(DISC_CX, DISC_CY, SRING_R + 1, SRING_R - 1, 0, 360, COL_FRAME);
  if (st == StreamState::PLAYING) {
    uint32_t buf = _p->getBufferLevel();
    float pct = (buf > 32768) ? 360.0f : buf * 360.0f / 32768.0f;
    if (pct > 2.0f) g->drawArc(DISC_CX, DISC_CY, SRING_R + 1, SRING_R - 1, 0, pct, COL_GREEN);
  } else if (st == StreamState::CONNECTING || st == StreamState::RECONNECTING) {
    float pos = fmodf((float)(millis() / 6), 360.0f);
    g->drawArc(DISC_CX, DISC_CY, SRING_R + 1, SRING_R - 1, pos, pos + 45, COL_YELLOW);
  }
}

void UI::drawMain() {
  Arduino_GFX* g = _d->getGfx();

  drawStatusLine();

  drawStatusRing();      // outer thin ring (buffer / activity)
  drawDisc();            // station disc
  drawVolumeRing();      // volume arc around the disc

  // station name
  const char* name = _s->getCurrentStation().name.c_str();
  char station[28];
  strncpy(station, name, sizeof(station) - 1);
  station[sizeof(station) - 1] = '\0';
  drawCenteredText(station, &fontMonob24, FONT_MONOB24_ASCENT, COL_WHITE, STATION_Y);

  // genre
  drawCenteredText(_s->getCurrentStation().genre.c_str(), &fontMono10,
                   FONT_MONO10_ASCENT, COL_GRAY, GENRE_Y);

  drawMetadataArea();
  drawBottomButtons();
}

void UI::drawMetadataArea() {
  Arduino_GFX* g = _d->getGfx();
  g->fillRect(30, META_Y - 4, 406, 2 * META_LINE_H + 8, COL_BG);

  char meta[160];
  _p->getMetadata(meta, sizeof(meta));
  if (meta[0] == '\0') {
    strcpy(meta, wifiIsAP() ? "Configure WiFi: 192.168.4.1"
                            : (_p->isPlaying() ? "Live stream" : "Connecting..."));
  }

  const GFXfont* f = &fontMono16;
  int adv = pgm_read_byte(&f->glyph['A' - f->first].xAdvance);   // mono
  int len = strlen(meta);
  if (len * adv > 372) {
    _marqueeActive = true;
    drawScrollingText(meta, META_Y, COL_LGRAY);
  } else {
    _marqueeActive = false;
    int maxLine = 372 / adv;
    int line = 0;
    for (int i = 0; i < len && line < 2; i += maxLine) {
      char buf[44];
      int n = len - i;
      if (n > maxLine) n = maxLine;
      memcpy(buf, meta + i, n);
      buf[n] = '\0';
      drawCenteredText(buf, f, FONT_MONO16_ASCENT, COL_LGRAY,
                       META_Y + line * META_LINE_H);
      line++;
    }
  }
}

void UI::drawScrollingText(const char* text, int y, uint16_t color) {
  Arduino_GFX* g = _d->getGfx();
  const GFXfont* f = &fontMono16;
  int adv = pgm_read_byte(&f->glyph['A' - f->first].xAdvance);
  int textW = textWidthPx(text, f);
  int offset = (millis() / MARQUEE_STEP_MS) % (textW + 372);
  g->setFont(f);
  g->setTextColor(color);
  int x = 47 + 372 - offset;
  int baseline = y + FONT_MONO16_ASCENT;
  for (int i = 0; text[i]; i++) {
    uint8_t c = (uint8_t)text[i];
    if (c >= f->first && c <= f->last) {
      if (x + adv > 47 && x < 419) {        // clip per char
        g->setCursor(x, baseline);
        g->print(text[i]);
      }
      x += adv;
    }
  }
}

void UI::drawBottomButtons() {
  Arduino_GFX* g = _d->getGfx();
  bool active = (_p->isPlaying() ||
                 _p->getState() == StreamState::CONNECTING ||
                 _p->getState() == StreamState::RECONNECTING);

  // stations button (left) — list icon
  g->fillCircle(BTN_LX, BTN_Y, BTN_R, COL_DARK);
  g->drawCircle(BTN_LX, BTN_Y, BTN_R, COL_WHITE);
  g->fillRect(BTN_LX - 12, BTN_Y - 11, 24, 3, COL_WHITE);
  g->fillRect(BTN_LX - 12, BTN_Y,     24, 3, COL_WHITE);
  g->fillRect(BTN_LX - 12, BTN_Y + 11, 24, 3, COL_WHITE);

  // play/stop button (center)
  g->fillCircle(BTN_CX, BTN_Y, BTN_CR, COL_DARK);
  g->drawCircle(BTN_CX, BTN_Y, BTN_CR, active ? COL_GREEN : COL_ORANGE);
  if (active) {
    g->fillRoundRect(BTN_CX - 14, BTN_Y - 14, 28, 28, 6, COL_WHITE);   // stop
  } else {
    g->fillTriangle(BTN_CX - 14, BTN_Y - 16, BTN_CX - 14, BTN_Y + 16,
                    BTN_CX + 22, BTN_Y, COL_WHITE);                    // play
  }

  // mute button (right)
  uint16_t mcol = _p->isMuted() ? COL_RED : COL_WHITE;
  g->fillCircle(BTN_RX, BTN_Y, BTN_R, COL_DARK);
  g->drawCircle(BTN_RX, BTN_Y, BTN_R, mcol);
  const char* ml = _p->isMuted() ? "MUTED" : "MUTE";
  int mw = textWidthPx(ml, &fontMono10);
  g->setFont(&fontMono10);
  g->setTextColor(mcol);
  g->setCursor(BTN_RX - mw / 2, BTN_Y - 4 + FONT_MONO10_ASCENT);
  g->print(ml);
}

void UI::drawStationList() {
  Arduino_GFX* g = _d->getGfx();
  drawCenteredText("STATIONS", &fontMonob24, FONT_MONOB24_ASCENT, COL_WHITE, LIST_TITLE_Y);

  // circular back button
  g->fillCircle(BACK_CX, BACK_CY, BACK_R, COL_DARK);
  g->drawCircle(BACK_CX, BACK_CY, BACK_R, COL_ORANGE);
  g->fillTriangle(BACK_CX - 13, BACK_CY, BACK_CX + 13, BACK_CY - 13,
                  BACK_CX + 13, BACK_CY + 13, COL_WHITE);

  uint8_t count = _s->getStationCount();
  uint8_t cur = _s->getCurrentIndex();
  for (int i = 0; i < LIST_VISIBLE; i++) {
    uint8_t idx = _listOffset + i;
    if (idx >= count) break;
    int y = LIST_Y0 + i * LIST_ROW_H;
    bool isCur = (idx == cur);

    g->fillRoundRect(LIST_X, y, LIST_W, LIST_ROW_HH, 18,
                     isCur ? COL_DIMBLUE : 0x1082);
    if (isCur) g->drawRoundRect(LIST_X, y, LIST_W, LIST_ROW_HH, 18, COL_ORANGE);
    g->fillCircle(LIST_X + 26, y + 29, 9, paletteColor(idx));

    String name = _s->getStation(idx).name;
    if (name.length() > 24) name = name.substring(0, 24);
    g->setFont(&fontMono16);
    g->setTextColor(isCur ? COL_WHITE : COL_LGRAY);
    g->setCursor(LIST_X + 48, y + 20 + FONT_MONO16_ASCENT);
    g->print(name);
  }

  char hint[48];
  snprintf(hint, sizeof(hint), "web: %s", wifiGetIP().c_str());
  drawCenteredText(hint, &fontMono10, FONT_MONO10_ASCENT, COL_GRAY, HINT_Y);
  char ind[12];
  snprintf(ind, sizeof(ind), "%u/%u", _listOffset + 1, count);
  drawCenteredText(ind, &fontMono10, FONT_MONO10_ASCENT, COL_LGRAY, IND_Y);
}

// ------------------------------------------------------------------
// Touch actions
// ------------------------------------------------------------------
void UI::handleTap(uint16_t x, uint16_t y) {
  LOG_I("ui", "tap %u,%u", x, y);
  if (_screen == Screen::MAIN) {
    float dC = hypotf((float)x - BTN_CX, (float)y - BTN_Y);
    float dL = hypotf((float)x - BTN_LX, (float)y - BTN_Y);
    float dR = hypotf((float)x - BTN_RX, (float)y - BTN_Y);
    if (dC <= BTN_CR + 8) {
      togglePlayStop();
    } else if (dL <= BTN_R + 8) {
      uint8_t count = _s->getStationCount();
      uint8_t maxOff = (count > LIST_VISIBLE) ? count - LIST_VISIBLE : 0;
      uint8_t cur = _s->getCurrentIndex();
      _listOffset = (cur >= 2 && cur <= maxOff + 1) ? cur - 2
                   : (cur > maxOff + 1 ? maxOff : 0);
      _screen = Screen::STATION_LIST;
      _dirty = true;
    } else if (dR <= BTN_R + 8) {
      toggleMute();
    }
  } else {  // STATION_LIST
    float dB = hypotf((float)x - BACK_CX, (float)y - BACK_CY);
    if (dB <= BACK_R + 8) {
      _screen = Screen::MAIN;
      _dirty = true;
    } else if (y >= LIST_Y0) {
      uint8_t idx = _listOffset + (y - LIST_Y0) / LIST_ROW_H;
      if (idx < _s->getStationCount()) selectStation(idx);
    }
  }
}

void UI::handleSwipe(int16_t dy) {
  if (_screen == Screen::MAIN) {
    if (dy < -40) _p->increaseVolume();
    else if (dy > 40) _p->decreaseVolume();
    else return;
    drawVolumeRing();
    drawStatusLine();
    return;
  }
  uint8_t count = _s->getStationCount();
  uint8_t maxOff = (count > LIST_VISIBLE) ? count - LIST_VISIBLE : 0;
  if (dy < -40 && _listOffset < maxOff) _listOffset++;
  else if (dy > 40 && _listOffset > 0) _listOffset--;
  else return;
  _dirty = true;
  LOG_I("ui", "list offset -> %u", _listOffset);
}

void UI::setVolumeFromXY(uint16_t x, uint16_t y) {
  float ang = atan2f((float)y - DISC_CY, (float)x - DISC_CX);   // -PI..PI
  if (ang < 0) ang += 6.2831853f;
  float start = 135.0f * 0.01745329f;                           // 2.356
  float frac = ang - start;
  if (frac < 0) frac += 6.2831853f;
  frac /= 4.7123890f;                                           // 270 deg
  if (frac > 1.0f) frac = 1.0f;
  uint8_t vol = (uint8_t)(frac * 100.0f + 0.5f);
  if (vol != _p->getVolume()) {
    _p->setVolume(vol);
    drawVolumeRing();
    drawStatusLine();
  }
}

void UI::selectStation(uint8_t index) {
  RadioStation st = _s->getStation(index);
  _s->setStation(index);
  LOG_I("ui", "select station %u: %s", index, st.name.c_str());
  _p->startStream(st.url.c_str());
  _screen = Screen::MAIN;
  _dirty = true;
}

void UI::togglePlayStop() {
  StreamState st = _p->getState();
  if (_p->isPlaying() || st == StreamState::CONNECTING ||
      st == StreamState::RECONNECTING) {
    LOG_I("ui", "stop pressed");
    _p->stopStream();
  } else {
    LOG_I("ui", "play pressed");
    _p->startStream(_s->getCurrentStation().url.c_str());
  }
  _dirty = true;
}

void UI::toggleMute() {
  _p->toggleMute();
  _dirty = true;
}
