/* Signal UI core: state machine, text/vector engine, input, actions.
 * Layout numbers come 1:1 from the 004-signal prototype CSS. */
#include "sig_ui.h"
#include "sig/signal_fonts_data.h"   // glyph data (sig_ui.cpp is the only TU with data)
#include "config.h"                  // TOUCH_SDA/SCL + XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"              // AXP2101 battery monitor (chip typed by config.h)
#include <WiFi.h>                    // RSSI for the signal bars
#include <Wire.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <math.h>

SigUI sigui;

const sig_font_t* SigUI::font(int i) { return SIG_FONTS[i]; }

/* Display extras per prototype STATIONS table (name/genre come from sm). */
const SigUI::SDef SigUI::SD[9] = {
  {"BBG",  "BLOOMBERG",  "RADIO",         "Surveillance",          "News & Talk"},
  {"NPR",  "NPR NEWS",   "AND CULTURE",   "All Things Considered", "News & Talk"},
  {"BBC",  "BBC WORLD",  "SERVICE",       "Global News Podcast",   "News & Talk"},
  {"WNYC", "WNYC",       "93.9 FM",       "The Brian Lehrer Show", "News & Talk"},
  {"KQED", "KQED",       "PUBLIC RADIO",  "Forum",                 "News & Talk"},
  {"CBC",  "CBC RADIO",  "ONE",           "As It Happens",         "News & Talk"},
  {"SOMA", "SOMAFM",     "GROOVE SALAD",  "Groove Salad",          "Music"},
  {"RP",   "RADIO",      "PARADISE",      "Main Mix",              "Music"},
  {"JAZ",  "JAZZ24",     "",              "Midnight Jazz",         "Music"},
};

/* =================================================================== */
/* small graphics helpers                                              */
/* =================================================================== */
uint16_t SigUI::blendC(uint16_t dst, uint16_t src, uint8_t a) {
  if (a >= 15) return src;
  if (a == 0) return dst;
  uint16_t dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
  uint16_t sr = (src >> 11) & 0x1F, sg = (src >> 5) & 0x3F, sb = src & 0x1F;
  uint16_t r = (sr * a + dr * (15 - a)) / 15;
  uint16_t g = (sg * a + dg * (15 - a)) / 15;
  uint16_t b = (sb * a + db * (15 - a)) / 15;
  return (r << 11) | (g << 5) | b;
}

void SigUI::fillR(int x, int y, int w, int h, int r, uint16_t c) {
  cv->fillRoundRect(x, y, w, h, r, c);
}
void SigUI::strokeR(int x, int y, int w, int h, int r, uint16_t c) {
  cv->drawRoundRect(x, y, w, h, r, c);
}
void SigUI::fillC(int cx, int cy, int r, uint16_t c) {
  cv->fillCircle(cx, cy, r, c);
}
void SigUI::ringC(int cx, int cy, int r, uint16_t c, float a0, float a1,
                  float th) {
  if (a0 == 0 && a1 == 360 && th == 1) { cv->drawCircle(cx, cy, r, c); return; }
  float prevx = cx + r * sinf(a0 * DEG_TO_RAD);
  float prevy = cy - r * cosf(a0 * DEG_TO_RAD);
  for (float a = a0 + 1; a <= a1; a += 1) {
    float x = cx + r * sinf(a * DEG_TO_RAD);
    float y = cy - r * cosf(a * DEG_TO_RAD);
    thickLine(prevx, prevy, x, y, th, c);
    prevx = x; prevy = y;
  }
}
void SigUI::thickLine(float x1, float y1, float x2, float y2, float w,
                      uint16_t c) {
  float dx = x2 - x1, dy = y2 - y1;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.01f) { fillC((int)x1, (int)y1, (int)(w / 2 + .5f), c); return; }
  float nx = -dy / len * w / 2, ny = dx / len * w / 2;
  uint16_t col[4];
  for (int i = 0; i < 4; i++) col[i] = c;
  /* quad via triangles */
  cv->fillTriangle((int)(x1 + nx), (int)(y1 + ny), (int)(x1 - nx),
                   (int)(y1 - ny), (int)(x2 + nx), (int)(y2 + ny), c);
  cv->fillTriangle((int)(x1 - nx), (int)(y1 - ny), (int)(x2 - nx),
                   (int)(y2 - ny), (int)(x2 + nx), (int)(y2 + ny), c);
  (void)col;
}

/* =================================================================== */
/* text engine                                                          */
/* =================================================================== */
static int utf8len(uint8_t b) {
  if (b < 0x80) return 1;
  if ((b & 0xE0) == 0xC0) return 2;
  if ((b & 0xF0) == 0xE0) return 3;
  return 4;
}

void SigUI::initFonts() {
  for (int f = 0; f < NF; f++) {
    const sig_font_t* F = SIG_FONTS[f];
    const char* p = F->chars;
    int i = 0;
    while (*p && i < NG) {
      int l = utf8len((uint8_t)*p);
      chOff[f][i] = p - F->chars;
      chLen[f][i] = l;
      p += l; i++;
    }
    for (; i < NG; i++) { chOff[f][i] = 0; chLen[f][i] = 0; }
  }
}

static bool glyphAt(const sig_font_t* F, const uint16_t* off,
                    const uint16_t* len, const char* p, int* gi) {
  int l = utf8len((uint8_t)*p);
  for (int i = 0; i < F->n; i++) {
    if (len[i] == l && memcmp(F->chars + off[i], p, l) == 0) { *gi = i; return true; }
  }
  return false;
}

float SigUI::tWidth(int fi, const char* s, float ls, float scale) {
  const sig_font_t* F = SIG_FONTS[fi];
  float w = 0;
  const char* p = s;
  while (*p) {
    if (utf8len((uint8_t)*p) == 3 && memcmp(p, "\xe2\x86\x92", 3) == 0) {
      w += F->px * 0.62f * scale + ls; p += 3; continue;
    }
    int gi;
    if (glyphAt(F, chOff[fi], chLen[fi], p, &gi))
      w += F->gl[gi].adv * scale + ls;
    p += utf8len((uint8_t)*p);
  }
  return w;
}

void SigUI::tDraw(int fi, const char* s, float x, float boxW, float cssTop,
                  uint16_t col, uint8_t align, float ls, uint16_t bg, float lh,
                  float scale, bool ellip, float maxW) {
  const sig_font_t* F = SIG_FONTS[fi];
  if (maxW <= 0) maxW = boxW;
  char buf[320];
  strncpy(buf, s, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  float w = tWidth(fi, buf, ls, scale);
  if (ellip && w > maxW && buf[0]) {
    /* chop characters until width + ellipsis fits */
    size_t bl = strlen(buf);
    while (bl > 0) {
      while (bl > 0 && ((uint8_t)buf[bl - 1] & 0xC0) == 0x80) bl--;
      if (bl > 0) bl--;
      buf[bl] = 0;
      w = tWidth(fi, buf, ls, scale);
      float we = tWidth(fi, "\xe2\x80\xa6", ls, scale);
      if (w + we <= maxW) {
        strcat(buf, "\xe2\x80\xa6");
        w += we;
        break;
      }
    }
  }
  float sx = x;
  if (align == 1) sx = x + (boxW - w) / 2;
  else if (align == 2) sx = x + boxW - w;

  float lineH = lh > 0 ? lh * F->px : (float)(F->ascent + F->descent + F->gap);
  float baseline = cssTop + (lineH - (F->ascent + F->descent)) / 2.0f +
                   F->ascent;
  float pen = sx;
  const char* p = buf;
  while (*p) {
    int n = utf8len((uint8_t)*p);
    if (n == 3 && memcmp(p, "\xe2\x86\x92", 3) == 0) {
      icArrowR(pen + F->px * 0.31f * scale, baseline - F->px * 0.32f * scale,
               col, F->px * 0.62f * scale);
      pen += F->px * 0.62f * scale + ls;
      p += 3; continue;
    }
    int gi;
    if (glyphAt(F, chOff[fi], chLen[fi], p, &gi)) {
      const sig_glyph_t* G = &F->gl[gi];
      if (G->w) {
        float gx0 = pen + G->xoff * scale;
        float gy0 = baseline + G->yoff * scale;
        int ow = (int)(G->w * scale), oh = (int)(G->h * scale);
        if (ow < 1 && G->w) ow = 1;
        if (oh < 1 && G->h) oh = 1;
        int rowb = (G->w + 1) / 2;
        for (int oy = 0; oy < oh; oy++) {
          int syi = (int)(oy / scale);
          if (syi >= G->h) syi = G->h - 1;
          int py = (int)(gy0 + oy);
          if (py < 0 || py >= 466) continue;
          for (int ox = 0; ox < ow; ox++) {
            int sxi = (int)(ox / scale);
            if (sxi >= G->w) sxi = G->w - 1;
            int px = (int)(gx0 + ox);
            if (px < 0 || px >= 466) continue;
            uint8_t byte = F->bmp[G->off + syi * rowb + (sxi >> 1)];
            uint8_t a = (sxi & 1) ? (byte & 0x0F) : (byte >> 4);
            if (!a) continue;
            a = (uint16_t)a * _ga / 15;
            if (!a) continue;
            uint16_t dst = bg ? blendC(bg, bg, 15) : cv->getFramebuffer()[py * 466 + px];
            cv->drawPixel(px, py, blendC(dst, col, a));
          }
        }
      }
      pen += G->adv * scale + ls;
    } else {
      pen += ls;                     /* unknown glyph: spacing only */
    }
    p += n;
  }
}

void SigUI::tDrawC(int fi, const char* s, float cx, float cssTop,
                   uint16_t col, float ls, uint16_t bg, float lh) {
  float w = tWidth(fi, s, ls);
  tDraw(fi, s, cx - w / 2, w, cssTop, col, 0, ls, bg, lh);
}

/* =================================================================== */
/* icons (prototype SVG geometry)                                      */
/* =================================================================== */
void SigUI::icWifiBars(float x, float y, uint16_t c, float thirdA) {
  cv->fillRoundRect((int)x, (int)(y + 8), 3, 5, 1, c);
  cv->fillRoundRect((int)(x + 5.6f), (int)(y + 5), 3, 8, 1, c);
  uint16_t cc = c;
  if (thirdA < 15) {                       /* dimmed third bar (wifi off) */
    uint16_t r = ((c >> 11) & 0x1F) * thirdA / 15;
    uint16_t g = ((c >> 5) & 0x3F) * thirdA / 15;
    uint16_t b = (c & 0x1F) * thirdA / 15;
    cc = (r << 11) | (g << 5) | b;
  }
  cv->fillRoundRect((int)(x + 11.2f), (int)y, 3, 13, 1, cc);
}
void SigUI::icBattery(float x, float y, uint16_t c, int pct) {
  cv->drawRoundRect((int)(x + .6f), (int)(y + .6f), 21, 11, 3, c);
  if (pct > 0) {                    // fill = real charge (0/unknown = empty)
    float wf = 19.2f * pct / 100.0f;
    if (wf < 1.0f) wf = 1.0f;
    if (wf > 19.2f) wf = 19.2f;
    cv->fillRoundRect((int)(x + 2.4f), (int)(y + 2.4f), (int)(wf + .5f), 7, 2, c);
  }
  cv->fillTriangle((int)(x + 23.2f), (int)(y + 4.2f), (int)(x + 23.2f),
                   (int)(y + 7.8f), (int)(x + 25), (int)(y + 6), c);
}
void SigUI::icList(float cx, float cy, uint16_t c) {
  float x0 = cx - 9.5f, y0 = cy - 9.5f, k = 19.f / 20;
  for (int i = 0; i < 3; i++) {
    float yy = y0 + (5.5f + i * 4.5f) * k;
    for (float xx = x0 + 7.5f * k; xx <= x0 + 16.5f * k; xx += 1)
      cv->drawPixel((int)xx, (int)yy, c);
    fillC((int)(x0 + 3.6f * k), (int)yy, 1, c);
  }
}
void SigUI::icPrev(float cx, float cy, uint16_t c) {
  float x0 = cx - 8.5f, y0 = cy - 8.5f, k = 17.f / 20;
  cv->fillRoundRect((int)(x0 + 4.6f * k), (int)(y0 + 5.4f * k), 3, 9, 1, c);
  cv->fillTriangle((int)(x0 + 16.4f * k), (int)(y0 + 6.2f * k),
                   (int)(x0 + 16.4f * k), (int)(y0 + 13.8f * k),
                   (int)(x0 + 8.6f * k), (int)(y0 + 10 * k), c);
}
void SigUI::icPlay(float cx, float cy, uint16_t c) {
  cv->fillTriangle((int)(cx - 10 + 7.2f), (int)(cy - 10 + 5.2f),
                   (int)(cx - 10 + 7.2f), (int)(cy - 10 + 14.8f),
                   (int)(cx - 10 + 15.4f), (int)(cy - 10 + 10), c);
}
void SigUI::icPause(float cx, float cy, uint16_t c) {
  cv->fillRoundRect((int)(cx - 10 + 6), (int)(cy - 10 + 5.4f), 4, 10, 1, c);
  cv->fillRoundRect((int)(cx - 10 + 11.2f), (int)(cy - 10 + 5.4f), 4, 10, 1, c);
}
void SigUI::icNext(float cx, float cy, uint16_t c) {
  float x0 = cx - 8.5f, y0 = cy - 8.5f, k = 17.f / 20;
  cv->fillTriangle((int)(x0 + 3.6f * k), (int)(y0 + 6.2f * k),
                   (int)(x0 + 3.6f * k), (int)(y0 + 13.8f * k),
                   (int)(x0 + 11.4f * k), (int)(y0 + 10 * k), c);
  cv->fillRoundRect((int)(x0 + 13.2f * k), (int)(y0 + 5.4f * k), 3, 9, 1, c);
}
void SigUI::icVol(float cx, float y, uint16_t c) {
  float x0 = cx - 9.5f, y0 = y - 9.5f, k = 19.f / 20;
  float P[6][2] = {{3.4f, 7.8f}, {6.3f, 7.8f}, {10.4f, 5},
                   {10.4f, 15}, {6.3f, 12.2f}, {3.4f, 12.2f}};
  int q[6][2];
  for (int i = 0; i < 6; i++) {
    q[i][0] = (int)(x0 + P[i][0] * k);
    q[i][1] = (int)(y0 + P[i][1] * k);
  }
  for (int i = 1; i <= 4; i++)
    cv->fillTriangle(q[0][0], q[0][1], q[i][0], q[i][1],
                     q[i + 1][0], q[i + 1][1], c);
  /* waves: concentric ")" arcs stamped with round dots ~0.8 px apart.
   * ringC's 1-degree thickLine segments were sub-pixel jitter at this
   * radius (r*1deg ≈ 0.07 px) — that raggedness is what read as ugly. */
  const float wcy = y0 + 10 * k;
  const float wcx = x0 + 9.3f * k;
  for (int wv = 0; wv < 2; wv++) {
    float r = (wv == 0) ? 4.2f : 7.4f;
    float a0 = (wv == 0) ? 52.0f : 50.0f;
    float a1 = (wv == 0) ? 128.0f : 130.0f;
    float step = 46.0f / r;                 /* degrees ≈ 0.8 px arc length */
    for (float a = a0; a <= a1 + 0.01f; a += step) {
      float rad = a * DEG_TO_RAD;
      fillC((int)lroundf(wcx + r * k * sinf(rad)),
            (int)lroundf(wcy - r * k * cosf(rad)), 1, c);
    }
  }
}
void SigUI::icBack(float cx, float cy, uint16_t c) {
  float x0 = cx - 9, y0 = cy - 9, k = 18.f / 20;
  thickLine(x0 + 12.4f * k, y0 + 4.6f * k, x0 + 6.4f * k, y0 + 10 * k, 2, c);
  thickLine(x0 + 6.4f * k, y0 + 10 * k, x0 + 12.4f * k, y0 + 15.4f * k, 2, c);
}
void SigUI::icGear(float cx, float cy, uint16_t c) {
  float x0 = cx - 9, y0 = cy - 9, k = 18.f / 20;
  /* prototype: stroke-width 1.6 round-cap lines M3 6h14 / M3 10h14 / M3 14h14
   * + black knobs r1.9; plain 1px drawPixel rows were visibly too thin. */
  for (int i = 0; i < 3; i++)
    thickLine(x0 + 3 * k, y0 + (6 + i * 4) * k, x0 + 17 * k,
              y0 + (6 + i * 4) * k, 1.6f, c);
  fillC((int)(x0 + 7.5f * k), (int)(y0 + 6 * k), 2, 0);
  fillC((int)(x0 + 13 * k), (int)(y0 + 10 * k), 2, 0);
  fillC((int)(x0 + 6 * k), (int)(y0 + 14 * k), 2, 0);
}
void SigUI::icRescan(float cx, float cy, uint16_t c, float rot) {
  float r = 6.6f;
  ringC((int)cx, (int)cy, (int)r, c, rot + 20, rot + 320, 2);
  float ax = cx + r * sinf((rot + 320) * DEG_TO_RAD);
  float ay = cy - r * cosf((rot + 320) * DEG_TO_RAD);
  thickLine(ax, ay, ax, ay - 3.6f, 2, c);
  thickLine(ax, ay - 3.6f, ax - 3.6f, ay - 3.6f, 2, c);
}
void SigUI::icLock(float x, float y, uint16_t c, float s) {
  float w = 12 * s, h = 14 * s;
  float bx = x, by = y + 5 * s;
  cv->fillRoundRect((int)bx, (int)by, (int)(9.2f * s), (int)(9 * s),
                    (int)(1.4f * s), c);
  cv->drawRoundRect((int)(bx + 2.6f * s), (int)by, (int)(4 * s),
                    (int)(5 * s), (int)(2 * s), c);
  (void)w; (void)h;
}
void SigUI::icShield(float cx, float cy, uint16_t c) {
  int x0 = cx - 17, y0 = cy - 17;
  int pts[6][2] = {{17, 1}, {4, 6}, {4, 14}, {17, 30}, {30, 14}, {30, 6}};
  for (int i = 0; i < 6; i++) {
    int j = (i + 1) % 6;
    thickLine(x0 + pts[i][0], y0 + pts[i][1], x0 + pts[j][0],
              y0 + pts[j][1], 2, c);
  }
  thickLine(x0 + 12, y0 + 17, x0 + 15, y0 + 20, 2, c);
  thickLine(x0 + 15, y0 + 20, x0 + 22, y0 + 12, 2, c);
}
void SigUI::icX(float cx, float cy, uint16_t c, float r) {
  thickLine(cx - r, cy - r, cx + r, cy + r, 2, c);
  thickLine(cx + r, cy - r, cx - r, cy + r, 2, c);
}
void SigUI::icEye(float cx, float cy, uint16_t c, bool off) {
  /* lens outline: parametric ellipse (GFX base has no ellipse call) —
   * strokes the same eye glyph the prototype uses in its SVG button */
  const float rx = 9.5f, ry = 5.5f;
  const int SEG = 24;
  float px = cx + rx, py = cy;
  for (int i = 1; i <= SEG; i++) {
    float a = 2.0f * PI * i / SEG;
    float x = cx + rx * cosf(a), y = cy + ry * sinf(a);
    cv->drawLine((int)px, (int)py, (int)x, (int)y, c);
    px = x; py = y;
  }
  cv->fillCircle((int)cx, (int)cy, 2, c);
  if (off) thickLine(cx - 11, cy - 7, cx + 11, cy + 7, 2, c);
}
void SigUI::icChevron(float cx, float cy, uint16_t c) {
  thickLine(cx - 3, cy - 4.5f, cx + 3, cy, 2, c);
  thickLine(cx + 3, cy, cx - 3, cy + 4.5f, 2, c);
}
void SigUI::icWifiOff(float cx, float cy, uint16_t c) {
  float x0 = cx - 18, y0 = cy - 14;
  cv->fillRoundRect((int)(x0 + 1), (int)(y0 + 9), 4, 5, 1, c);
  cv->fillRoundRect((int)(x0 + 7), (int)(y0 + 6), 4, 8, 1, c);
  cv->fillRoundRect((int)(x0 + 13), (int)(y0 + 1), 4, 13, 1, c);
  thickLine(x0 + 1.5f, y0 + 14.2f, x0 + 15.8f, y0 + .8f, 3, 0);
  thickLine(x0 + 1.5f, y0 + 14.2f, x0 + 15.8f, y0 + .8f, 2, c);
}
void SigUI::icShift(float cx, float cy, uint16_t c) {
  thickLine(cx, cy + 7, cx, cy - 6, 2, c);
  thickLine(cx - 7, cy - 1, cx, cy - 8, 2, c);
  thickLine(cx, cy - 8, cx + 7, cy - 1, 2, c);
  thickLine(cx - 6, cy + 7, cx + 6, cy + 7, 2, c);
}
void SigUI::icBksp(float cx, float cy, uint16_t c) {
  int x0 = cx - 11, y0 = cy - 7;
  int pts[5][2] = {{0, 0}, {15, 0}, {22, 7}, {15, 14}, {0, 14}};
  for (int i = 0; i < 5; i++) {
    int j = (i + 1) % 5;
    thickLine(x0 + pts[i][0], y0 + pts[i][1], x0 + pts[j][0],
              y0 + pts[j][1], 1.6f, c);
  }
  thickLine(x0 + 7, y0 + 4, x0 + 13, y0 + 10, 1.6f, c);
  thickLine(x0 + 13, y0 + 4, x0 + 7, y0 + 10, 1.6f, c);
}
void SigUI::icArrowR(float cx, float cy, uint16_t c, float sz) {
  float h = sz * .5f;
  thickLine(cx - sz / 2, cy, cx + sz / 2, cy, MAX(1.5f, sz * .11f), c);
  thickLine(cx + sz / 2, cy, cx + sz / 2 - h * .55f, cy - h * .55f,
            MAX(1.5f, sz * .11f), c);
  thickLine(cx + sz / 2, cy, cx + sz / 2 - h * .55f, cy + h * .55f,
            MAX(1.5f, sz * .11f), c);
}

/* =================================================================== */
/* begin / state                                                       */
/* =================================================================== */
void SigUI::loadState() {
  Preferences p;
  if (p.begin("sig", true)) {
    cur = p.getUChar("cur", 0);
    vol = p.getUChar("vol", 62);
    bright = p.getFloat("bright", 1.0f);
    sleepIdx = p.getUChar("sleep", 0);
    p.end();
  }
  if (cur < 0 || cur > 8) cur = 0;
}
void SigUI::saveState() {
  Preferences p;
  if (p.begin("sig", false)) {
    p.putUChar("cur", cur);
    p.putUChar("vol", vol);
    p.putFloat("bright", bright);
    p.putUChar("sleep", sleepIdx);
    p.end();
  }
}

/* =================================================================== */
/* live status: AXP2101 battery % + Wi-Fi RSSI bars (status line).     */
/* Values are real hardware readings — never literals (old hardcode:   */
/* "78%" + always-full bars). Refreshed once a second by tickTimers.   */
/* =================================================================== */
static XPowersPMU g_pmu;

static int battFromMv(int mv) {
  static const int16_t mvT[6] = {3300, 3500, 3700, 3850, 4000, 4200};
  static const int8_t  pcT[6] = {  0,   12,   42,   62,   82,  100};
  if (mv <= mvT[0]) return 0;
  if (mv >= mvT[5]) return 100;
  for (int i = 1; i < 6; i++)
    if (mv <= mvT[i])
      return pcT[i - 1] + (int)((long)(mv - mvT[i - 1]) *
                                (pcT[i] - pcT[i - 1]) / (mvT[i] - mvT[i - 1]));
  return 100;
}

bool SigUI::refreshStatus() {
  if (!pmuInit) {                       // one-shot probe (before touch task)
    pmuInit = g_pmu.begin(Wire, AXP2101_SLAVE_ADDRESS,
                          TOUCH_SDA, TOUCH_SCL) ? 1 : 2;
    if (pmuInit == 1) {                 // vendor adcOn() (Waveshare 01_PMU example):
      g_pmu.enableTemperatureMeasure();
      g_pmu.enableBattDetection();
      g_pmu.enableVbusVoltageMeasure();
      g_pmu.enableBattVoltageMeasure();
      g_pmu.enableSystemVoltageMeasure();
    }
    Serial.printf("[pmu] AXP2101 %s\n",
                  pmuInit == 1 ? "online" : "offline - battery unknown");
  }
  int8_t pct = -1;
  if (pmuInit == 1 && g_pmu.isBatteryConnect()) {
    int p = g_pmu.getBatteryPercent();   // chip-computed % (reg 0xA4)
    if (p < 0 && g_pmu.getBattVoltage() > 0) p = battFromMv(g_pmu.getBattVoltage());
    pct = (int8_t)(p > 100 ? 100 : p);
  }
  int8_t lvl;
  if (!wcPowered())        lvl = -1;    // radio off            -> dim icon
  else if (!wcConnected()) lvl = 0;     // on, waiting for link -> hint bars
  else {
    int r = WiFi.RSSI();
    lvl = r >= -55 ? 3 : r >= -70 ? 2 : 1;
  }
  bool chg = (pct != battPct) || (lvl != sigLvl);
  if (chg) {
    battPct = pct; sigLvl = lvl;
    Serial.printf("[batt] %d%% bars %d rssi %d | st1=0x%02X pct=%d "
                  "v=%d,%d adc=0x%02X det=0x%02X\n",
                  pct, lvl, wcConnected() ? (int)WiFi.RSSI() : 0,
                  pmuInit == 1 ? g_pmu.readRegister(XPOWERS_AXP2101_STATUS1) : -1,
                  pmuInit == 1 ? g_pmu.readRegister(XPOWERS_AXP2101_BAT_PERCENT_DATA) : -1,
                  pmuInit == 1 ? g_pmu.readRegister(XPOWERS_AXP2101_ADC_DATA_RELUST0) : -1,
                  pmuInit == 1 ? g_pmu.readRegister(XPOWERS_AXP2101_ADC_DATA_RELUST1) : -1,
                  pmuInit == 1 ? g_pmu.readRegister(XPOWERS_AXP2101_ADC_CHANNEL_CTRL) : -1,
                  pmuInit == 1 ? g_pmu.readRegister(XPOWERS_AXP2101_BAT_DET_CTRL) : -1);
  }
  return chg;
}

void SigUI::begin(DisplayUI* d, StreamPlayer* p, StationManager* s) {
  disp = d; pl = p; sm = s;
  g = disp->getGfx();

  /* Two 466x466 RGB565 framebuffers (434 KB each): the Canvas default
   * aligned_alloc() serves INTERNAL RAM only (fits neither), and begin()
   * must not re-run panel/bus init that display.begin() already did.
   * -> allocate both buffers in PSRAM and skip the output begin. */
  struct PsramCanvas : Arduino_Canvas {
    PsramCanvas(int16_t w, int16_t h, Arduino_G* o) : Arduino_Canvas(w, h, o) {}
    bool attach() {
      size_t s = (size_t)_width * (size_t)_height * 2;
      uint16_t* p =
          (uint16_t*)heap_caps_malloc(s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!p) return false;
      _framebuffer = p;
      return true;
    }
  };
  auto mkCanvas = [&](Arduino_G* out, const char* nm) -> Arduino_Canvas* {
    PsramCanvas* c = new PsramCanvas(466, 466, out);
    if (!c->attach()) {
      Serial.printf("[ui] %s: PSRAM fb alloc FAILED\n", nm);
      Serial.flush();
      return nullptr;
    }
    if (!c->begin(GFX_SKIP_OUTPUT_BEGIN)) {
      Serial.printf("[ui] %s: canvas begin FAILED\n", nm);
      Serial.flush();
      return nullptr;
    }
    return c;
  };
  cv = mkCanvas(g, "main");
  vcv = mkCanvas(g, "view");
  Serial.printf("[ui] canvases: %s\n", (cv && vcv) ? "2x PSRAM OK" : "FAIL");
  Serial.flush();

  initFonts();
  Serial.printf("[ui] fonts OK\n"); Serial.flush();
  loadState();
  pl->setVolume(vol);
  sm->setStation(cur);                 // single station index source
  disp->setBrightness((uint8_t)(bright * 255));
  rot = -cur * 40.0f;
  bootT0 = millis();
  view = V_BOOT;
  dirty = true;
  refreshStatus();                 // battery + signal before first paint
  Serial.printf("[ui] begin done\n"); Serial.flush();
  touch.startTask();
  Serial.printf("[ui] touch task started\n"); Serial.flush();
}

/* =================================================================== */
/* timers + pump                                                       */
/* =================================================================== */
void SigUI::tickTimers() {
  uint32_t now = millis();

  /* boot splash: exactly like replayBoot() — 1500 ms then now + connect */
  if (view == V_BOOT && now - bootT0 >= 1500) {
    setView(V_NOW);
    startConnect();
  }

  /* dial rotation animation (520 ms ease-out) */
  if (rotAnim) {
    float t = (now - rotT0) / 520.0f;
    if (t >= 1) { t = 1; rotAnim = false; }
    float e = 1 - powf(1 - t, 3);
    rot = rotFrom + (rotTo - rotFrom) * e;
    dirty = true;
  }

  /* dial release -> select after 260 ms (prototype setTimeout) */
  if (pendingSel >= 0 && (int32_t)(now - pendingT0) >= 0) {
    int i = pendingSel; pendingSel = -1;
    selectStation(i);
  }

  /* waveform / ambient animation cadence */
  /* waveform animates only while audio plays — a paused flat line
   * doesn't need 18fps idle repaints that saturate the loop */
  if (view == V_NOW && playing && now - waveT0 >= 55) {
    waveT0 = now;
    dirty = true;
  }
  if (view == V_BOOT && now - animT0 >= 80) { animT0 = now; dirty = true; }
  if (ovl == O_CON && now - animT0 >= 80) { animT0 = now; dirty = true; }
  if (view == V_WIFI && warea == 0 && now - animT0 >= 80) {
    animT0 = now; dirty = true;
  }
  if (view == V_STANDBY && now - animT0 >= 250) { animT0 = now; dirty = true; }
  if (ripT0 && now - ripT0 >= 460) { ripT0 = 0; dirty = true; }

  /* volume auto-hide */
  if (ovl == O_VOL && now - volHideT0 >= 2600) hideVol();

  /* connecting overlay resolution */
  if (ovl == O_CON && connecting) {
    if (conForWifi) {
      if (wcConnected()) {
        connecting = false;
        setOvl(O_NONE);
        toast((String("CONNECTED \xc2\xb7 ") + passSsid).c_str());
        if (playing) startConnect();
        dirty = true;
      } else if (wcJoinFailed() || now - conT0 > 16000) {
        connecting = false;
        setOvl(O_NONE);
        toast((String("COULDN'T CONNECT \xc2\xb7 ") + passSsid).c_str());
        dirty = true;
      }
    } else {
      if (pl->isPlaying()) {
        connecting = false;
        playing = true;
        setOvl(O_NONE);
        dirty = true;
      } else if (now - conT0 > (wcConnected() ? 15000 : 1600)) {
        /* no network: fail fast to the error screen (prototype = 1.6s)
         * instead of staring at the arc for 15s; with wifi, keep the
         * 15s grace for slow streams */
        showError();
      }
    }
  }

  /* keep playing flag in sync with the stream engine */
  if (!connecting && ovl != O_ERR) {
    bool ip = pl->isPlaying();
    if (ip != playing) { playing = ip; dirty = true; }
  }

  /* wifi page scan resolution (JS: list appears at >=1100 ms) */
  if (view == V_WIFI && warea == 0) {
    if (!scanDone && wcScanReady()) scanDone = true;
    if ((scanDone && now - scanT0 >= 1100) || now - scanT0 > 6000) {
      warea = 2; wlistY = 0; dirty = true;
    }
  }

  /* toast */
  if (toastT0 && now - toastT0 >= 2400) { toastT0 = 0; dirty = true; }

  /* sleep timer */
  if (sleepArmed && now >= sleepT0) {
    sleepArmed = false;
    pl->stopStream();
    playing = false;
    setView(V_STANDBY);
  }

  /* clock tick (status + standby) */
  static uint32_t lastClock = 0;
  if (now - lastClock >= 1000) {
    lastClock = now;
    refreshStatus();               // battery % + RSSI bars (real readings)
    if (view == V_NOW || view == V_STANDBY) dirty = true;
  }
}

void SigUI::pump() {
  console();
  /* touch is polled by the 5 ms task (touch.startTask); pump only
   * consumes queued edge events — a slow render can no longer eat a tap */
  /* crosshair: full-scene repaint while visible (only when enabled),
   * plus exactly one repaint to clear it when it expires */
  static bool wasXh = false;
  bool xhNow = s_showXh && touch.markerActive();
  if (xhNow || (wasXh && !xhNow)) dirty = true;
  wasXh = xhNow;
  onTouch();
  tickTimers();
  wcLoop();
  if (dirty) { render(); dirty = false; }
}

/* =================================================================== */
/* serial console (host-side debugging over USB CDC)                   */
/*   D = dump the visible framebuffer as binary PPM                    */
/*   T = touch I2C diagnosis   P = state snapshot                      */
/* =================================================================== */
void SigUI::console() {
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch < 0) break;
    switch ((char)ch) {
      case 'D': case 'd':
        dumpFrame();
        break;
      case 'T': case 't':
        Serial.print("[touch] ");
        Serial.println(touch.diag());
        break;
      case 'M': case 'm':
        touch.monitor(8000);
        break;
      case 'E': case 'e': {
        char eb[16]; int bi = 0;
        uint32_t t0 = millis();
        while (millis() - t0 < 120 && bi < 15) {
          if (Serial.available() > 0) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') break;
            eb[bi++] = c;
          }
        }
        eb[bi] = 0;
        int ex = 0, ey = 0;
        if (sscanf(eb, "%d,%d", &ex, &ey) == 2 && ex >= 0 && ex < 466 &&
            ey >= 0 && ey < 466) {
          touch.injectTap((uint16_t)ex, (uint16_t)ey);
          Serial.printf("[inj] tap %d,%d\n", ex, ey);
        } else {
          Serial.println("[inj] usage: E x,y");
        }
        break;
      }
      case 'X': case 'x':
        s_showXh = !s_showXh;
        Serial.printf("[X] crosshair %s\n", s_showXh ? "ON" : "OFF");
        break;
      case 'G': case 'g': {
        char gb[32]; int bi = 0;
        uint32_t t0 = millis();
        while (millis() - t0 < 150 && bi < 31) {
          if (Serial.available() > 0) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') break;
            gb[bi++] = c;
          }
        }
        gb[bi] = 0;
        int gx1, gy1, gx2, gy2;
        if (sscanf(gb, "%d,%d,%d,%d", &gx1, &gy1, &gx2, &gy2) == 4 &&
            gx1 >= 0 && gx1 < 466 && gx2 >= 0 && gx2 < 466 &&
            gy1 >= 0 && gy1 < 466 && gy2 >= 0 && gy2 < 466) {
          touch.injectDrag((uint16_t)gx1, (uint16_t)gy1,
                           (uint16_t)gx2, (uint16_t)gy2, 10);
          Serial.printf("[inj] drag %d,%d -> %d,%d\n", gx1, gy1, gx2, gy2);
        } else {
          Serial.println("[inj] usage: G x1,y1,x2,y2");
        }
        break;
      }
      case 'P': case 'p':
        Serial.printf("[P] pong\n");
        Serial.printf(
            "[state] view=%u cur=%d (%s) vol=%u bright=%.2f play=%d "
            "conn=%d ovl=%u wifi=%d/%d batt=%d%% bars=%d pmu=%u ssid=%s heap=%u\n",
            view, cur, SD[cur].code, vol, bright, playing, connecting, ovl,
            wcPowered(), wcConnected(), battPct, sigLvl, pmuInit,
            wcSsid().c_str(), (unsigned)ESP.getFreeHeap());
        break;
      default:
        break;   // ignore (line noise / newlines)
    }
  }
}

void SigUI::dumpFrame() {
  if (!cv) { Serial.println("[dump] no framebuffer"); return; }
  uint16_t* fb = cv->getFramebuffer();
  if (!fb) { Serial.println("[dump] null framebuffer"); return; }
  /* bulk dump: briefly allow blocking TX (host is reading) — with the
   * console's normal setTxTimeoutMs(0) the USBCDC would drop most bytes */
  Serial.setTxTimeoutMs(5000);
  Serial.printf("P6\n466 466\n255\n");
  static uint8_t row[466 * 3];
  for (int y = 0; y < 466; y++) {
    for (int x = 0; x < 466; x++) {
      uint16_t v = fb[y * 466 + x];
      row[x * 3 + 0] = (uint8_t)((v >> 11) & 0x1F) << 3;
      row[x * 3 + 1] = (uint8_t)((v >> 5) & 0x3F) << 2;
      row[x * 3 + 2] = (uint8_t)(v & 0x1F) << 3;
    }
    Serial.write(row, sizeof(row));
  }
  Serial.printf("\n[dump] frame flushed\n");
  Serial.setTxTimeoutMs(0);      // back to non-blocking console
}

/* =================================================================== */
/* render                                                               */
/* =================================================================== */
void SigUI::paintView() {
  switch (view) {
    case V_BOOT:     paintBoot(); break;
    case V_STANDBY:  paintStandby(); break;
    case V_NOW:      paintNow(); break;
    case V_STATIONS: paintStations(); break;
    case V_SETTINGS: paintSettings(); break;
    case V_WIFI:     paintWifiView(); break;
  }
}

unsigned long g_renderT0 = 0;
bool SigUI::s_showXh = false;           // crosshair overlay (console 'X')

void SigUI::render() {
  if (!cv || !vcv) return;              // canvas alloc failed earlier
  g_renderT0 = millis();
  Arduino_Canvas* front = cv;
  bool swipe = (dragMode == DRAG_V && fabsf(viewShiftY) > 0.5f &&
                view == V_NOW && vcv);
  uint32_t sa0 = millis();
  if (swipe) {
    /* view layer shifted + dimmed; dial ring stays fixed behind it */
    cv = vcv;
    memset(vcv->getFramebuffer(), 0, 466 * 466 * 2);   /* fillScreen() is a per-pixel loop (~5s!) */
    paintView();
    cv = front;
    memset(front->getFramebuffer(), 0, 466 * 466 * 2);
    paintDial(true);
    int sh = (int)viewShiftY;
    float dim = 1.0f - fabsf(viewShiftY) / 130.0f;
    /* direct framebuffer writes — the old per-pixel drawPixel loop cost
     * ~200ms per swipe frame (each call = bounds check + write); raw
     * indexed stores do the same math in ~1-2ms */
    uint16_t* __restrict df = front->getFramebuffer();
    const uint16_t* __restrict sf = vcv->getFramebuffer();
    for (int y = 0; y < 466; y++) {
      int src = y - sh;
      if (src < 0 || src >= 466) continue;
      const uint16_t* srow = sf + src * 466;
      uint16_t* drow = df + y * 466;
      for (int x = 0; x < 466; x++) {
        uint16_t v = srow[x];
        if (!v) continue;                       /* transparent → dial shows */
        uint16_t rr = ((v >> 11) & 0x1F) * dim;
        uint16_t gg = ((v >> 5) & 0x3F) * dim;
        uint16_t bb = (v & 0x1F) * dim;
        drow[x] = (rr << 11) | (gg << 5) | bb;
      }
    }
  } else {
    memset(front->getFramebuffer(), 0, 466 * 466 * 2);
    if (view == V_NOW) paintDial(true);
    paintView();
  }
  uint32_t sa1 = millis();

  /* overlays */
  switch (ovl) {
    case O_CON: paintCon(); break;
    case O_ERR: paintErr(); break;
    case O_VOL: paintVol(); break;
    case O_PASS: paintPass(); break;
    case O_NET: paintNet(); break;
    default: break;
  }

  /* toast (wifi screen) */
  if (view == V_WIFI && toastT0 && ovl == O_NONE) paintToast();

  /* ripple (below overlays visually, drawn after view) */
  if (ripT0) {
    float t = (millis() - ripT0) / 450.0f;
    if (t < 1) {
      float rr = 23 * t;
      uint8_t a = (uint8_t)(3.5f * (1 - t));
      for (float ang = 0; ang < 360; ang += 6) {
        for (float r2 = rr - 3; r2 <= rr; r2 += 1.5f) {
          if (r2 < 0) continue;
          int px = (int)(ripX + r2 * sinf(ang * DEG_TO_RAD));
          int py = (int)(ripY - r2 * cosf(ang * DEG_TO_RAD));
          if (px < 0 || px >= 466 || py < 0 || py >= 466) continue;
          uint16_t dst = cv->getFramebuffer()[py * 466 + px];
          cv->drawPixel(px, py, blendC(dst, 0xFFFF, a));
        }
      }
    }
  }

  /* touch crosshair: live white marker (console 'X' toggles — each
   * visible marker frame costs a full repaint+flush, so it's opt-in) */
  if (s_showXh && touch.markerActive()) {
    uint16_t mx = 0, my = 0;
    if (touch.markerPos(mx, my)) {
      cv->drawLine((int)mx - 12, (int)my, (int)mx + 12, (int)my, 0xFFFF);
      cv->drawLine((int)mx, (int)my - 12, (int)mx, (int)my + 12, 0xFFFF);
      cv->drawCircle((int)mx, (int)my, 6, 0xFFFF);
    }
  }

  uint32_t sa2 = millis();
  if (sa2 - sa0 > 150)
    Serial.printf("[stages] base=%ums ovl+fx=%ums\n",
                  (unsigned)(sa1 - sa0), (unsigned)(sa2 - sa1));

  /* frame perf accounting: body = scene paint, flush = push to panel */
  {
    static uint32_t accB = 0, accF = 0, n = 0, mx = 0, last = 0;
    extern unsigned long g_renderT0;
    uint32_t body = millis() - g_renderT0;
    uint32_t t1 = millis();
    cv->flush();
    uint32_t ft = millis() - t1;
    accB += body; accF += ft; n++;
    if (body > mx) mx = body;
    if (!last) last = millis();
    if (millis() - last >= 2000 && n) {
      Serial.printf("[perf] frames=%u avgPaint=%ums avgFlush=%ums maxPaint=%ums\n",
                    (unsigned)n, (unsigned)(accB / n), (unsigned)(accF / n),
                    (unsigned)mx);
      accB = accF = n = mx = 0; last = millis();
    }
  }

  static bool firstFrame = false;       // crash-bracketing breadcrumb
  if (!firstFrame) {
    firstFrame = true;
    Serial.printf("[ui] first frame flushed, view=%d\n", view);
    Serial.flush();
  }
}
