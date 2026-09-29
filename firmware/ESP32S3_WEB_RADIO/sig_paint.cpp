/* Signal UI painters: boot, standby, now, stations, settings, connect,
 * error, volume. All coordinates taken verbatim from the prototype CSS. */
#include "sig_ui.h"
#include <math.h>
#include <time.h>

static void upstr(char* d, const char* s, size_t n) {
  size_t i = 0;
  for (; s[i] && i + 1 < n; i++)
    d[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
  d[i] = 0;
}

/* =================================================================== */
void SigUI::paintBoot() {
  /* SIGNAL — disp 800/46, ls .34em, text-indent .34em (shifts +indent/2) */
  float ls = 46 * 0.34f;
  float w = tWidth(F_DISP_800_46, "SIGNAL", ls);
  tDraw(F_DISP_800_46, "SIGNAL", (466 - w) / 2 + ls / 2, w, 186, C_INK,
        0, ls, 0, 1);

  char sub[32];
  upstr(sub, "Internet Radio", sizeof(sub));
  float ls2 = 9.5f * 0.46f;
  float w2 = tWidth(F_MONO_400_9_5, sub, ls2);
  tDraw(F_MONO_400_9_5, sub, (466 - w2) / 2 + ls2 / 2, w2, 248, C_INK2,
        0, ls2, 0, 1);

  /* spinner: r20 arc 120.3° (dash 42 of 125.66), 1.1 s/turn */
  float spin = fmodf((millis() - bootT0) / 1100.0f, 1.0f) * 360.0f;
  int cy = 292 + 23;
  ringC(233, cy, 20, H565(0x4A3010), spin, spin + 120, 4);   /* glow-ish */
  ringC(233, cy, 20, C_ACCENT, spin, spin + 120, 2);
}

/* =================================================================== */
void SigUI::paintStandby() {
  /* time */
  String hm = disp->getCurrentTime();
  tDraw(F_DISP_800_96, hm.c_str(), 0, 466, 146, C_INK, 1, -96 * 0.03f,
        0, 1);

  /* date: "MON · SEP 28" (skip until NTP has set the clock) */
  struct tm ti;
  if (getLocalTime(&ti, 0) && ti.tm_year + 1900 > 2020) {
    char wd[8] = {0}, mo[8] = {0}, dstr[24];
    strftime(wd, sizeof(wd), "%a", &ti);
    strftime(mo, sizeof(mo), "%b", &ti);
    snprintf(dstr, sizeof(dstr), "%s \xc2\xb7 %s %d", wd, mo, ti.tm_mday);
    char up[32];
    upstr(up, dstr, sizeof(up));
    float ls = 12 * 0.34f;
    float w = tWidth(F_MONO_400_12, up, ls);
    tDraw(F_MONO_400_12, up, (466 - w) / 2, w, 262, C_INK2, 0, ls, 0, 1);
  }

  /* rule */
  cv->fillRect(191, 296, 84, 1, H565(0x383838));

  /* air row: dot + ON AIR + station (gap 9, centered) */
  const char* stat = playing ? "ON AIR" : "PAUSED";
  RadioStation s = sm->getStation(cur);
  char stn[48];
  snprintf(stn, sizeof(stn), "%s%s%s", SD[cur].l1,
           SD[cur].l2[0] ? " " : "", SD[cur].l2);
  (void)s;
  float ls = 10.5f * 0.22f;
  float w1 = tWidth(F_MONO_500_10_5, stat, ls);
  float w2 = tWidth(F_MONO_500_10_5, stn, ls);
  float total = 5 + 9 + w1 + 9 + w2;
  float x = (466 - total) / 2;
  uint16_t c = playing ? C_LIVE : C_PAUSE;
  uint8_t a = 15;
  if (playing) {                       /* pulse 1.5 s */
    float p = fmodf(millis() / 1500.0f, 1.0f);
    a = (uint8_t)(15 * (0.55f + 0.45f * sinf(p * TWO_PI)));
  }
  /* dot */
  uint16_t dd = blendC(0, c, a);
  fillC((int)(x + 2), 314 + 6, 3, dd);
  float txtW = w1;
  tDraw(F_MONO_500_10_5, stat, x + 5 + 9, txtW, 314, c, 0, ls, 0, 1);
  tDraw(F_MONO_500_10_5, stn, x + 5 + 9 + w1 + 9, w2, 314, C_INK2, 0, ls,
        0, 1);
}

/* =================================================================== */
void SigUI::paintDial(bool full) {
  (void)full;
  /* guide circles */
  cv->drawCircle(233, 233, 230, H565(0x121212));
  cv->drawCircle(233, 233, 190, H565(0x0D0D0D));

  int near = ((int)lroundf(-rot / 40.0f) % 9 + 9) % 9;

  /* ticks rotate with rot */
  for (int a = 0; a < 360; a += 5) {
    float wa = a + rot;
    /* major tick per station label (prototype maj = a % N, N = 360/9 = 40) */
    bool maj = (a % 40) == 0;
    float r1 = maj ? 213 : 221;
    float sa = sinf(wa * DEG_TO_RAD), ca = cosf(wa * DEG_TO_RAD);
    float x1 = 233 + r1 * sa, y1 = 233 - r1 * ca;
    float x2 = 233 + 229 * sa, y2 = 233 - 229 * ca;
    if (maj) {
      int idx = (a / 40) % 9;
      if (idx == near) {
        thickLine(x1, y1, x2, y2, 5, blendC(0, C_ACCENT, 4));  /* glow */
        thickLine(x1, y1, x2, y2, 3, C_ACCENT);
      } else {
        thickLine(x1, y1, x2, y2, 2, H565(0x575757));
      }
    } else {
      thickLine(x1, y1, x2, y2, 1, H565(0x212121));
    }
  }

  /* labels at r200 (upright, not rotated) */
  for (int j = 0; j < 9; j++) {
    float th = (j * 40 + rot) * DEG_TO_RAD;
    float x = 233 + 200 * sinf(th);
    float y = 233 - 200 * cosf(th);
    if (j == near) {
      float ls = 13.5f * 0.06f;
      float w = tWidth(F_MONO_600_13_5, SD[j].code, ls);
      tDrawC(F_MONO_600_13_5, SD[j].code, x, y - 13.5f / 2 - 4,
             blendC(0, C_ACCENT, 5), ls);                      /* glow */
      tDrawC(F_MONO_600_13_5, SD[j].code, x, y - 13.5f / 2 - 4,
             C_ACCENT, ls);
      (void)w;
    } else {
      float ls = 12 * 0.06f;
      tDrawC(F_MONO_500_12, SD[j].code, x, y - 12 / 2 - 4,
             H565(0xA8A8A8), ls);
    }
  }

  /* caret at top: polygon 233,19 226,7 240,7 */
  cv->fillTriangle(233, 19, 226, 7, 240, 7, blendC(0, C_ACCENT, 5));
  cv->fillTriangle(233, 17, 227, 7, 239, 7, C_ACCENT);
}

/* =================================================================== */
void SigUI::paintNow() {
  /* ---- status line (y54 h24, gap 11, centered) ---- */
  String hm = disp->getCurrentTime();
  char batt[8];
  if (battPct >= 0) snprintf(batt, sizeof(batt), "%d%%", battPct);
  else              strlcpy(batt, "--%", sizeof(batt));
  float ls = 13 * 0.0f;                    /* no tracking on status */
  float wc = tWidth(F_MONO_500_13, hm.c_str(), ls);
  float wb = tWidth(F_MONO_500_13, batt, ls);
  float total = 17 + 11 + wc + 11 + (25 + 4 + wb);
  float x = (466 - total) / 2;
  float cy = 54 + 12;
  /* wifi icon: real signal level (sigLvl: -1 off · 0 no link · 1..3 bars).
   * Bars below the level are full, the rest keep the .45 hint look. */
  uint16_t barA[3];
  for (int i = 0; i < 3; i++) {
    uint8_t a = (sigLvl < 0) ? 5 : (i < sigLvl ? 15 : (uint8_t)(15 * 0.45f));
    barA[i] = blendC(0, C_STATUS, a);
  }
  cv->fillRoundRect((int)x, (int)(cy - 6.5f + 8), 3, 5, 1, barA[0]);
  cv->fillRoundRect((int)(x + 5.6f), (int)(cy - 6.5f + 5), 3, 8, 1, barA[1]);
  cv->fillRoundRect((int)(x + 11.2f), (int)(cy - 6.5f), 3, 13, 1, barA[2]);
  float xc = x + 17 + 11;
  tDraw(F_MONO_500_13, hm.c_str(), xc, wc, cy - 13 / 2.0f, C_STATUS, 0,
        ls, 0, 1);
  float xb = xc + wc + 11;
  icBattery(xb, cy - 6, C_STATUS, battPct);
  tDraw(F_MONO_500_13, batt, xb + 25 + 4, wb, cy - 13 / 2.0f, C_STATUS, 0,
        ls, 0, 1);

  /* ---- tally ---- */
  const char* txt = connecting ? "TUNING" : (playing ? "ON AIR" : "PAUSED");
  float lsl = 11 * 0.22f;
  float wt = tWidth(F_MONO_500_11, txt, lsl);
  float boxw = 28 + 6 + 8 + wt;            /* pad 14*2 + dot6 + gap8 */
  float bx = (466 - boxw) / 2;
  uint16_t bg, bd, tc;
  bool pausedStyle = !playing;
  if (connecting) pausedStyle = true;
  bg = pausedStyle ? C_PAUSEBG : C_LIVESOFT;
  bd = pausedStyle ? C_PAUSELN : C_LIVELINE;
  tc = pausedStyle ? C_PAUSE : C_LIVE;
  cv->fillRoundRect((int)bx, 98, (int)boxw, 26, 13, bg);
  cv->drawRoundRect((int)bx, 98, (int)boxw, 26, 13, bd);
  uint8_t da = 15;
  if (playing) {
    float p = fmodf(millis() / 1500.0f, 1.0f);
    da = (uint8_t)(15 * (0.55f + 0.45f * sinf(p * TWO_PI)));
  }
  fillC((int)(bx + 14 + 3), 111, 3, blendC(0, tc, da));
  tDraw(F_MONO_500_11, txt, bx + 14 + 6 + 8, wt, 105.5f, tc, 0, lsl, 0, 1);

  /* ---- headline (36/800, box 74..392, lines .99) ---- */
  float longest = 0;
  const char* LL[2] = {SD[cur].l1, SD[cur].l2};
  for (int i = 0; i < 2; i++) {
    const char* L = LL[i];
    if (!L[0]) continue;
    float w = tWidth(F_DISP_800_36, L, 0);
    if (w > longest) longest = w;
  }
  float scale = 1;
  if (longest > 318) scale = MAX(24.0f, floorf(36 * 318 / longest)) / 36.0f;
  float top = 138;
  for (int i = 0; i < 2; i++) {
    const char* L = LL[i];
    if (!L[0]) continue;
    float w = tWidth(F_DISP_800_36, L, 0, scale);
    tDraw(F_DISP_800_36, L, 74 + (318 - w) / 2, w, top, C_INK, 0, 0, 0,
          0.99f, scale);
    top += 0.99f * 36 * scale;
  }

  /* ---- prog (Newsreader italic 22) ---- */
  tDraw(F_SERIF_400_22, SD[cur].prog, 56, 354, 224, C_PROG, 1, 0, 0, 1.3f,
        1, true);

  /* ---- genre (mono 500/10.5, uppercase, accent .92) ---- */
  char g[64], gu[64];
  RadioStation s = sm->getStation(cur);
  snprintf(g, sizeof(g), "%s", s.genre.c_str());
  upstr(gu, g, sizeof(gu));
  float lsg = 10.5f * 0.22f;
  tDraw(F_MONO_500_10_5, gu, 40, 386, 258, blendC(0, C_ACCENT, 14), 1, lsg,
        0, 1, 1, true);

  /* ---- waveform (240x40 at 113,276) ---- */
  static float wt2 = 0, wsmooth = 0;
  float amp = playing ? (6.5f + vol / 100.0f * 9.5f) : 0.7f;
  wsmooth += (amp - wsmooth) * 0.06f;
  wt2 += 0.055f;
  float px[73], py[73];
  for (int i = 0; i <= 72; i++) {
    float xw = i / 72.0f * 240;
    float env = sinf(xw / 240 * PI) * 0.75f + 0.25f;
    float yw = 20 - (sinf(xw * .13f + wt2 * 3.1f) +
                     sinf(xw * .055f - wt2 * 2.0f) * .62f +
                     sinf(xw * .21f + wt2 * 1.35f) * .38f) *
                       wsmooth * env;
    px[i] = 113 + xw;
    py[i] = 276 + yw;
  }
  for (int i = 0; i < 72; i++)
    thickLine(px[i], py[i], px[i + 1], py[i + 1], 5,
              blendC(0, C_ACCENT, 3));
  for (int i = 0; i < 72; i++)
    thickLine(px[i], py[i], px[i + 1], py[i + 1], 2, C_ACCENT);

  /* ---- transport buttons ---- */
  struct B { float cx, cy, r; int id; };
  B bs[] = {{128.8f, 340.9f, 22, 1}, {174.4f, 371.1f, 24, 2},
            {233, 383, 27, 3},      {291.6f, 371.1f, 24, 4},
            {337.2f, 340.9f, 22, 5}};
  for (auto& b : bs) {
    bool isPlay = b.id == 3;
    bool dis = isPlay && connecting;
    if (isPlay) {
      uint16_t bgc = dis ? blendC(0, C_INK, 6) : C_INK;
      fillC((int)b.cx, (int)b.cy, (int)b.r, bgc);
      uint16_t ic = dis ? blendC(0, 0x0A0A0B, 6) : 0x0A0A0B;
      if (playing) icPause(b.cx, b.cy, ic);
      else icPlay(b.cx, b.cy, ic);
    } else {
      fillC((int)b.cx, (int)b.cy, (int)b.r, C_SURF);
      cv->drawCircle((int)b.cx, (int)b.cy, (int)(b.r - 1), C_LINE);
      uint16_t ic = dis ? blendC(0, C_ICON, 6) : C_ICON;
      switch (b.id) {
        case 1: icList(b.cx, b.cy, ic); break;
        case 2: icPrev(b.cx, b.cy, ic); break;
        case 4: icNext(b.cx, b.cy, ic); break;
        case 5: icVol(b.cx, b.cy, ic); break;
      }
    }
  }
}

/* =================================================================== */
/* station list content walk (positions shared with hit testing)       */
static void walkStations(int y0[9], int secY[2], int* contentH) {
  int y = 0;
  int ns = 0;
  for (int i = 0; i < 9; i++) {
    if (i == 0 || strcmp(SigUI::SD[i].sec, SigUI::SD[i - 1].sec) != 0) {
      if (ns < 2) secY[ns] = y;
      ns++;
      y += 34;
    }
    y0[i] = y;
    y += 64;
    if (i < 8 && strcmp(SigUI::SD[i + 1].sec, SigUI::SD[i].sec) == 0)
      y += 13;
  }
  *contentH = y;
}

void SigUI::paintStations() {
  /* header */
  char title[24];
  upstr(title, "Stations", sizeof(title));
  float ls = 11.5f * 0.30f;
  float w = tWidth(F_MONO_400_11_5, title, ls);
  tDraw(F_MONO_400_11_5, title, (466 - w) / 2, w, 92 - 11.5f / 2, C_INK2,
        0, ls, 0, 1);
  icBack(126, 92, C_INK2);
  icGear(340, 92, C_INK2);

  /* content */
  int y0[9], secY[2], contentH = 0;
  walkStations(y0, secY, &contentH);
  int base = 116 + (int)listY;

  char up[40];
  upstr(up, SD[0].sec, sizeof(up));
  tDraw(F_MONO_400_9, up, 87, 300, base + secY[0] + 11.5f, C_SEC, 0,
        9 * 0.32f, 0, 1);
  upstr(up, SD[6].sec, sizeof(up));
  tDraw(F_MONO_400_9, up, 87, 300, base + secY[1] + 11.5f, C_SEC, 0,
        9 * 0.32f, 0, 1);

  for (int i = 0; i < 9; i++) {
    int ry = base + y0[i];
    if (ry > 380 || ry + 64 < 116) continue;
    bool isCur = (i == cur);
    /* mono40 circle at x87 */
    int mcx = 87 + 20, mcy = ry + 32;
    if (isCur) {
      fillC(mcx, mcy, 20, H565(0x050505));
      cv->drawCircle(mcx, mcy, 20, C_ACCENT);
      float lsc = 10.5f * 0.04f;
      float wc = tWidth(F_MONO_600_10_5, SD[i].code, lsc);
      tDraw(F_MONO_600_10_5, SD[i].code, mcx - wc / 2, wc, mcy - 10.5f / 2,
            C_ACCENT, 0, lsc, 0, 1);
    } else {
      cv->drawCircle(mcx, mcy, 20, H565(0x292929));
      float lsc = 10.5f * 0.04f;
      float wc = tWidth(F_MONO_600_10_5, SD[i].code, lsc);
      tDraw(F_MONO_600_10_5, SD[i].code, mcx - wc / 2, wc, mcy - 10.5f / 2,
            C_MONO40, 0, lsc, 0, 1);
    }
    /* text block */
    RadioStation s = sm->getStation(i);
    float lsn = 15.5f * 0.005f;
    bool showAir = isCur && playing;
    float wn = tWidth(F_DISP_600_15_5, s.name.c_str(), lsn);
    float nameMax = 240 - (showAir ? 10 + tWidth(F_MONO_600_8_5, "ON AIR",
                                                  8.5f * 0.18f) : 0);
    float nmLine = font(F_DISP_600_15_5)->ascent + font(F_DISP_600_15_5)->descent +
                   font(F_DISP_600_15_5)->gap;
    float subLine = font(F_MONO_400_10)->ascent + font(F_MONO_400_10)->descent +
                    font(F_MONO_400_10)->gap;
    float txtTop = ry + (64 - (nmLine + 3 + subLine)) / 2;
    tDraw(F_DISP_600_15_5, s.name.c_str(), 139, nameMax, txtTop, C_INK, 0,
          lsn, 0, 0, true, nameMax);
    if (showAir) {
      float lsa = 8.5f * 0.18f;
      float wa = tWidth(F_MONO_600_8_5, "ON AIR", lsa);
      float xa = 139 + MIN(wn, nameMax) + 10;
      tDraw(F_MONO_600_8_5, "ON AIR", xa, wa, txtTop + (nmLine - 8.5f) / 2,
            C_LIVE, 0, lsa, 0, 1);
    }
    char sub[64], sup[64];
    snprintf(sub, sizeof(sub), "%s", s.genre.c_str());
    upstr(sup, sub, sizeof(sup));
    float lss = 10 * 0.14f;
    tDraw(F_MONO_400_10, sup, 139, 240, txtTop + nmLine + 3, C_SUB, 0, lss,
          0, 0, true);
  }

  /* scrollbar */
  if (contentH > 268) {
    int thh = MAX(26, 264 * 264 / contentH);
    float frac = (contentH - 264) ? (-listY) / (contentH - 264) : 0;
    cv->fillRoundRect(399, 116, 3, 264, 1, H565(0x171717));
    cv->fillRoundRect(399, 116 + (int)(frac * (264 - thh)), 3, thh, 1,
                      H565(0x858585));
  }

  /* mask: fade top 11% / bottom 13% of the 264 px window */
  for (int y = 116; y < 380; y++) {
    int d = y - 116;
    float f = 1.0f;
    if (d < 29) f = d / 29.0f;
    else if (d > 229) f = (264 - d) / 35.0f;
    if (f >= 1) continue;
    for (int x = 0; x < 466; x++) {
      uint16_t v = cv->getFramebuffer()[y * 466 + x];
      uint16_t r = ((v >> 11) & 0x1F) * f;
      uint16_t g = ((v >> 5) & 0x3F) * f;
      uint16_t b = (v & 0x1F) * f;
      cv->drawPixel(x, y, (r << 11) | (g << 5) | b);
    }
  }
}

/* =================================================================== */
void SigUI::paintSettings() {
  char title[24];
  upstr(title, "Settings", sizeof(title));
  float ls = 11.5f * 0.30f;
  float w = tWidth(F_MONO_400_11_5, title, ls);
  tDraw(F_MONO_400_11_5, title, (466 - w) / 2, w, 92 - 11.5f / 2, C_INK2,
        0, ls, 0, 1);
  icBack(126, 92, C_INK2);

  const char* nm[4] = {"Wi-Fi", "Brightness", "Sleep timer", "About"};
  const char* sub[4] = {"Connected \xc2\xb7 2.4 GHz", "On \xc2\xb7 2.4 GHz",
                        "2.4 GHz \xc2\xb7 802.11 b/g/n"};
  bool conn = wcConnected() && wcPowered();
  bool on = wcPowered();
  const char* subTxt = conn ? sub[0] : (on ? sub[1] : sub[2]);
  /* wifi row sub differs per JS paintWifi for settings: same strings */
  char nmup[4][24], subup[48];
  for (int i = 0; i < 4; i++) upstr(nmup[i], nm[i], sizeof(nmup[i]));

  int base = 116 + (int)listY;
  for (int i = 0; i < 4; i++) {
    int ry = base + i * 77;
    if (ry > 380 || ry + 64 < 116) continue;
    float nmLine = font(F_DISP_600_14_5)->ascent + font(F_DISP_600_14_5)->descent +
                   font(F_DISP_600_14_5)->gap;
    float subLine = font(F_MONO_400_10)->ascent + font(F_MONO_400_10)->descent +
                    font(F_MONO_400_10)->gap;
    float txtTop = ry + (64 - (nmLine + 3 + subLine)) / 2;
    float lsn = 14.5f * 0.0f;
    tDraw(F_DISP_600_14_5, nmup[i], 87, 200, txtTop, C_INK, 0, lsn, 0, 1);
    if (i == 0) {
      upstr(subup, subTxt, sizeof(subup));
      tDraw(F_MONO_400_10, subup, 87, 240, txtTop + nmLine + 3, C_SUB, 0,
            10 * 0.14f, 0, 1);
    } else if (i == 1) {
      upstr(subup, "AMOLED \xc2\xb7 700 nits max", sizeof(subup));
      tDraw(F_MONO_400_10, subup, 87, 240, txtTop + nmLine + 3, C_SUB, 0,
            10 * 0.14f, 0, 1);
    } else if (i == 2) {
      upstr(subup, "Stop playback after", sizeof(subup));
      tDraw(F_MONO_400_10, subup, 87, 240, txtTop + nmLine + 3, C_SUB, 0,
            10 * 0.14f, 0, 1);
    } else {
      upstr(subup, "ESP32-S3R8 \xc2\xb7 8 MB PSRAM", sizeof(subup));
      tDraw(F_MONO_400_10, subup, 87, 240, txtTop + nmLine + 3, C_SUB, 0,
            10 * 0.14f, 0, 1);
    }

    float valTop = ry + 32 - 11.5f / 2;
    float lsv = 11.5f * 0.08f;
    if (i == 0) {
      String vs = conn ? wcSsid() : String(on ? "None" : "Off");
      float wv = tWidth(F_MONO_400_11_5, vs.c_str(), lsv);
      tDraw(F_MONO_400_11_5, vs.c_str(), 359 - wv, wv, valTop, C_INK2, 0,
            lsv, 0, 1);
      icChevron(372, ry + 32, C_INK2);
    } else if (i == 1) {
      char pct[8];
      snprintf(pct, sizeof(pct), "%d%%", (int)roundf(bright * 100));
      float wp = tWidth(F_MONO_400_11_5, pct, lsv);
      tDraw(F_MONO_400_11_5, pct, 379 - wp, wp, valTop, C_INK2, 0, lsv, 0, 1);
      int tx = 379 - (int)wp - 7 - 112;
      int ty = ry + 28;
      cv->fillRoundRect(tx, ty, 112, 7, 3, H565(0x242424));
      float f = (bright - 0.5f) / 0.5f;
      if (f > 0) cv->fillRoundRect(tx, ty, (int)(f * 112), 7, 3, C_ACCENT);
      fillC(tx + (int)(f * 112), ty + 3, 7, C_INK);
    } else if (i == 2) {
      char v[12];
      if (sleepIdx == 0) snprintf(v, sizeof(v), "OFF");
      else {
        static const int mins[4] = {0, 15, 30, 60};
        snprintf(v, sizeof(v), "%d MIN", mins[sleepIdx]);
      }
      float wv = tWidth(F_MONO_400_11_5, v, lsv);
      tDraw(F_MONO_400_11_5, v, 379 - wv, wv, valTop,
            sleepIdx ? C_ACCENT : C_INK2, 0, lsv, 0, 1);
    } else {
      const char* v = "v0.1";
      float wv = tWidth(F_MONO_400_11_5, v, lsv);
      tDraw(F_MONO_400_11_5, v, 379 - wv, wv, valTop, C_INK2, 0, lsv, 0, 1);
    }
  }

  /* scrollbar + mask (content 299) */
  int contentH = 299;
  if (contentH > 268) {
    int thh = MAX(26, 264 * 264 / contentH);
    float frac = (contentH - 264) ? (-listY) / (contentH - 264) : 0;
    cv->fillRoundRect(399, 116, 3, 264, 1, H565(0x171717));
    cv->fillRoundRect(399, 116 + (int)(frac * (264 - thh)), 3, thh, 1,
                      H565(0x858585));
  }
  for (int y = 116; y < 380; y++) {
    int d = y - 116;
    float f = 1.0f;
    if (d < 29) f = d / 29.0f;
    else if (d > 229) f = (264 - d) / 35.0f;
    if (f >= 1) continue;
    for (int x = 0; x < 466; x++) {
      uint16_t v = cv->getFramebuffer()[y * 466 + x];
      uint16_t r = ((v >> 11) & 0x1F) * f;
      uint16_t g = ((v >> 5) & 0x3F) * f;
      uint16_t b = (v & 0x1F) * f;
      cv->drawPixel(x, y, (r << 11) | (g << 5) | b);
    }
  }
}

/* =================================================================== */
void SigUI::paintCon() {
  cv->fillScreen(0x0000);
  /* ring: 112px circle at (233,192), r55, 90° arc top+right, 1.05 s spin */
  float spin = fmodf((millis() - conT0) / 1050.0f, 1.0f) * 360.0f;
  ringC(233, 192, 55, blendC(0, C_ACCENT, 4), -135 + spin, -45 + spin, 4);
  ringC(233, 192, 55, C_ACCENT, -135 + spin, -45 + spin, 2);

  /* disc 88 at (233,192) */
  fillC(233, 192, 44, C_CARDBG);
  cv->drawCircle(233, 192, 43, H565(0x1F1F1F));
  const char* code = conForWifi ? "WI-FI" : SD[cur].code;
  float lsc = 15 * 0.06f;
  float wc = tWidth(F_MONO_600_15, code, lsc);
  tDraw(F_MONO_600_15, code, 233 - wc / 2, wc, 192 - 15 / 2.0f, C_ACCENT, 0,
        lsc, 0, 1);

  const char* lbl = conForWifi ? "Joining" : "Connecting";
  char up[24];
  upstr(up, lbl, sizeof(up));
  float lsl = 11 * 0.34f;
  float wl = tWidth(F_MONO_400_11, up, lsl);
  tDraw(F_MONO_400_11, up, 233 - wl / 2, wl, 268, C_ACCENT, 0, lsl, 0, 1);

  const char* nm = conForWifi ? passSsid.c_str()
                              : sm->getStation(cur).name.c_str();
  float wn = tWidth(F_DISP_600_16, nm, 0);
  tDraw(F_DISP_600_16, nm, 233 - wn / 2, wn, 294, C_INK, 0, 0, 0, 1);

  const char* hint = "Tap to cancel";
  float lsh = 9.5f * 0.2f;
  float wh = tWidth(F_MONO_400_9_5, hint, lsh);
  tDraw(F_MONO_400_9_5, hint, 233 - wh / 2, wh, 330, C_CONHINT, 0, lsh, 0, 1);
}

/* =================================================================== */
void SigUI::paintErr() {
  cv->fillScreen(0x0000);
  const char* nm = sm->getStation(cur).name.c_str();
  float wn = tWidth(F_DISP_600_15, nm, 0);
  tDraw(F_DISP_600_15, nm, 233 - wn / 2, wn, 148, C_STATUS, 0, 0, 0, 1);

  char t[40];
  upstr(t, "Stream unavailable", sizeof(t));
  float lst = 22 * 0.02f;
  float wt = tWidth(F_DISP_700_22, t, lst);
  tDraw(F_DISP_700_22, t, 233 - wt / 2, wt, 186, C_LIVE, 0, lst, 0, 1);

  const char* sub;
  if (!wcPowered()) sub = "Wi-Fi is off \xe2\x80\x94 enable it in Settings";
  else if (!wcConnected())
    sub = "No network \xe2\x80\x94 join one in Settings \xe2\x86\x92 Wi-Fi";
  else sub = "The station did not respond";
  float lss = 10.5f * 0.14f;
  tDraw(F_MONO_400_10_5, sub, 0, 466, 224, C_INK2, 1, lss, 0, 1);

  int r[2][4];
  errBtnRects(r);
  /* retry */
  cv->fillRoundRect(r[0][0], r[0][1], r[0][2], r[0][3], 23, C_ACCENT);
  char rt[16], sk[24];
  upstr(rt, "Retry", sizeof(rt));
  upstr(sk, "Next station", sizeof(sk));
  float lsb = 11.5f * 0.16f;
  float wr = tWidth(F_MONO_600_11_5, rt, lsb);
  tDraw(F_MONO_600_11_5, rt, r[0][0] + (r[0][2] - wr) / 2, wr, 264 + 17.25f,
        H565(0x0B0B0C), 0, lsb, 0, 1);
  /* skip */
  cv->drawRoundRect(r[1][0], r[1][1], r[1][2], r[1][3], 23, H565(0x5C5C5C));
  float ws = tWidth(F_MONO_600_11_5, sk, lsb);
  tDraw(F_MONO_600_11_5, sk, r[1][0] + (r[1][2] - ws) / 2, ws, 264 + 17.25f,
        C_INK, 0, lsb, 0, 1);
}

/* =================================================================== */
void SigUI::paintVol() {
  cv->fillScreen(0x0000);
  /* 25 segments, angle 120+k*5, r136..160, stroke 6 round caps */
  for (int k = 0; k < 25; k++) {
    float a = (120 + k * 5) * DEG_TO_RAD;
    float sa = sinf(a), ca = cosf(a);
    float x1 = 233 + 136 * sa, y1 = 233 - 136 * ca;
    float x2 = 233 + 160 * sa, y2 = 233 - 160 * ca;
    bool on = vol >= 100 - k * (100 / 24.0f) - 0.001f;
    if (on) {
      thickLine(x1, y1, x2, y2, 9, blendC(0, C_ACCENT, 4));
      thickLine(x1, y1, x2, y2, 6, C_ACCENT);
      fillC((int)x1, (int)y1, 3, C_ACCENT);
      fillC((int)x2, (int)y2, 3, C_ACCENT);
    } else {
      thickLine(x1, y1, x2, y2, 6, H565(0x2E2E2E));
      fillC((int)x1, (int)y1, 3, H565(0x2E2E2E));
      fillC((int)x2, (int)y2, 3, H565(0x2E2E2E));
    }
  }
  /* icon + label + number */
  icVol(233, 168, C_INK2);
  char lb[12];
  upstr(lb, "Volume", sizeof(lb));
  float lsl = 10.5f * 0.4f;
  float wl = tWidth(F_MONO_400_10_5, lb, lsl);
  tDraw(F_MONO_400_10_5, lb, 233 - wl / 2 + lsl / 2, wl, 196, C_INK2, 0,
        lsl, 0, 1);

  char num[8];
  snprintf(num, sizeof(num), "%d", vol);
  float lsn = 60 * 0.0f;
  float wn = tWidth(F_MONO_600_60, num, lsn);
  tDraw(F_MONO_600_60, num, 233 - wn / 2, wn, 214, C_INK, 0, lsn, 0, 1);
}
