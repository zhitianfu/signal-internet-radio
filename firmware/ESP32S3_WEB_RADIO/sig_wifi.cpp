/* Signal UI — Wi-Fi page, password keyboard, network card, toast.
 * Coordinates/layout from the prototype CSS (.wrow/.warea/.wifiwin/.kb/...). */
#include "sig_ui.h"
#include <math.h>

static void up2(char* d, const char* s, size_t n) {
  size_t i = 0;
  for (; s[i] && i + 1 < n; i++)
    d[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
  d[i] = 0;
}

/* row content vertical offset (shared calc) */
static float rowTxtTop(int rowTop, int rowH, const sig_font_t* nm,
                       const sig_font_t* sub) {
  float nmLine = nm->ascent + nm->descent + nm->gap;
  float subLine = sub->ascent + sub->descent + sub->gap;
  return rowTop + (rowH - (nmLine + 3 + subLine)) / 2;
}

/* mask + scrollbar for a list window */
static void listChrome(Arduino_Canvas* cv, int winTop, int winH,
                       float listY, float contentH) {
  if (contentH > winH + 4) {
    int thh = MAX(26, (int)(winH * winH / contentH));
    float frac = (contentH - winH) ? (-listY) / (contentH - winH) : 0;
    cv->fillRoundRect(399, winTop, 3, winH, 1, 0x18E3);
    cv->fillRoundRect(399, winTop + (int)(frac * (winH - thh)), 3, thh, 1,
                      0x8410);
  }
  int fadeTop = (int)(winH * 0.11f);
  int fadeBot = (int)(winH * 0.87f);
  for (int y = winTop; y < winTop + winH; y++) {
    int d = y - winTop;
    float f = 1.0f;
    if (d < fadeTop) f = (float)d / fadeTop;
    else if (d > fadeBot) f = (float)(winH - d) / (winH - fadeBot);
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
void SigUI::paintWifiView() {
  /* header */
  char title[24];
  up2(title, "Wi-Fi", sizeof(title));
  float ls = 11.5f * 0.30f;
  float w = tWidth(F_MONO_400_11_5, title, ls);
  tDraw(F_MONO_400_11_5, title, (466 - w) / 2, w, 92 - 11.5f / 2, C_INK2, 0,
        ls, 0, 1);
  icBack(126, 92, C_INK2);
  icRescan(340, 92, C_INK2, 0);

  bool on = wcPowered();
  bool conn = on && wcConnected();

  /* master row */
  cv->fillRoundRect(73, 118, 320, 56, 14, H565(0x0A0A0A));
  cv->drawRoundRect(73, 118, 320, 56, 14, C_LINE);
  float nmLine = font(F_DISP_600_15_5)->ascent + font(F_DISP_600_15_5)->descent +
                 font(F_DISP_600_15_5)->gap;
  float subLine = font(F_MONO_400_9_5)->ascent + font(F_MONO_400_9_5)->descent +
                  font(F_MONO_400_9_5)->gap;
  float wtop = 118 + (56 - (nmLine + 3 + subLine)) / 2;
  tDraw(F_DISP_600_15_5, "Wi-Fi", 89, 200, wtop, C_INK, 0, 15.5f * 0.0f, 0, 1);
  char sub[64], subu[64];
  if (conn) snprintf(sub, sizeof(sub), "Connected \xc2\xb7 %s",
                     wcSsid().c_str());
  else if (on) snprintf(sub, sizeof(sub), "On \xc2\xb7 no network");
  else snprintf(sub, sizeof(sub), "Off");
  up2(subu, sub, sizeof(subu));
  tDraw(F_MONO_400_9_5, subu, 89, 240, wtop + nmLine + 3, C_SUB, 0,
        9.5f * 0.1f, 0, 1);
  /* switch */
  int swx = 325, swy = 131;
  cv->fillRoundRect(swx, swy, 52, 30, 15, on ? C_ACCENT : H565(0x242424));
  fillC(swx + (on ? 37 : 15), swy + 15, 12, H565(0xEDEAE4));

  /* area */
  if (warea == 0) {
    /* scanning spinner + label (flex centered in 178..380) */
    float spin = fmodf((millis() - scanT0) / 1000.0f, 1.0f) * 360.0f;
    int scx = 233, scy = 178 + (202 - 69) / 2 + 20;
    ringC(scx, scy, 18, H565(0x40300F), -45 + spin, 45 + spin, 3);  /* dim */
    ringC(scx, scy, 18, C_ACCENT, -135 + spin, -45 + spin, 3);
    char t[24];
    up2(t, "Scanning", sizeof(t));
    float l2 = 11 * 0.3f;
    float wt = tWidth(F_MONO_400_11, t, l2);
    tDraw(F_MONO_400_11, t, 233 - wt / 2 + l2 / 2, wt, 300.5f, C_ACCENT, 0,
          l2, 0, 1);
  } else if (warea == 1) {
    icWifiOff(233, 233.5f, C_OFFIC);
    char t[24];
    up2(t, "Wi-Fi is off", sizeof(t));
    float l2 = 11 * 0.3f;
    float wt = tWidth(F_MONO_400_11, t, l2);
    tDraw(F_MONO_400_11, t, 233 - wt / 2 + l2 / 2, wt, 264.5f, C_SUB, 0, l2,
          0, 1);
    int r[4];
    wonBtnRect(r);
    cv->fillRoundRect(r[0], r[1], r[2], r[3], 22, C_ACCENT);
    char b[16];
    up2(b, "Turn on", sizeof(b));
    float lb = 11 * 0.16f;
    float wb = tWidth(F_MONO_600_11, b, lb);
    tDraw(F_MONO_600_11, b, r[0] + (r[2] - wb) / 2, wb, r[1] + 16.5f,
          H565(0x0B0B0C), 0, lb, 0, 1);
  } else {
    /* network list */
    auto& nets = wcResults();
    int n = (int)nets.size();
    float contentH = n ? n * 56.0f + (n - 1) * 12.0f : 0;
    String curSsid = wcSsid();
    for (int i = 0; i < n; i++) {
      int ry = 178 + i * 68 + (int)wlistY;
      if (ry > 380 || ry + 56 < 178) continue;
      WcNet& nw = nets[i];
      bool isCur = conn && nw.ssid == curSsid;
      uint8_t a15 = nw.five ? 6 : 15;               /* .42 dim */
      auto C = [&](uint16_t c) { return blendC(0, c, a15); };
      /* bars: base white25%, active by level, accent when connected */
      uint16_t dimb = C(H565(0x404040));
      uint16_t lit = C(isCur ? C_ACCENT : C_INK);
      int bx = 87, by = ry + 21;
      cv->fillRoundRect(bx, by + 8, 3, 5, 1, nw.lvl >= 1 ? lit : dimb);
      cv->fillRoundRect(bx + 6, by + 5, 3, 8, 1, nw.lvl >= 2 ? lit : dimb);
      cv->fillRoundRect(bx + 11, by, 3, 13, 1, nw.lvl >= 3 ? lit : dimb);
      /* text */
      float lsn = 15.5f * 0.005f;
      float wn = tWidth(F_DISP_600_15_5, nw.ssid.c_str(), lsn);
      float tagW = 0;
      if (nw.hot) {
        tagW = 7 + (4 + 4 + tWidth(F_MONO_600_7_5, "HOTSPOT",
                                   7.5f * 0.14f) + 4);
      }
      float nameMax = 379 - 14 - 116 - (isCur || nw.five || nw.open
                                         ? 70 : 20) - tagW;
      if (nameMax < 40) nameMax = 40;
      float txtTop = rowTxtTop(ry, 56, font(F_DISP_600_15_5), font(F_MONO_400_9_5));
      tDraw(F_DISP_600_15_5, nw.ssid.c_str(), 116, nameMax, txtTop,
            C(C_INK), 0, lsn, 0, 0, true, nameMax);
      if (nw.hot) {
        char t[12];
        up2(t, "hotspot", sizeof(t));
        float ltag = 7.5f * 0.14f;
        float wtag = tWidth(F_MONO_600_7_5, t, ltag);
        float tx = 116 + MIN(wn, nameMax) + 7;
        float ty = txtTop + (font(F_DISP_600_15_5)->ascent +
                             font(F_DISP_600_15_5)->descent +
                             font(F_DISP_600_15_5)->gap) - 7.5f - 4;
        cv->drawRoundRect((int)tx, (int)ty, (int)(wtag + 8), 12, 4,
                          C(H565(0x734C0D)));
        tDraw(F_MONO_600_7_5, t, tx + 4, wtag, ty + 1.5f, C(C_ACCENT), 0,
              ltag, 0, 1);
      }
      /* sub */
      char sub[48], su[48];
      if (nw.five) snprintf(sub, sizeof(sub), "5 GHz \xc2\xb7 unsupported");
      else snprintf(sub, sizeof(sub), "%s \xc2\xb7 2.4 GHz",
                    nw.open ? "Open" : "WPA2");
      up2(su, sub, sizeof(su));
      float nmLine2 = font(F_DISP_600_15_5)->ascent + font(F_DISP_600_15_5)->descent +
                      font(F_DISP_600_15_5)->gap;
      tDraw(F_MONO_400_9_5, su, 116, 240, txtTop + nmLine2 + 3, C(C_SUB), 0,
            9.5f * 0.1f, 0, 1);
      /* right badge */
      float lv = 9.5f * 0.14f;
      if (isCur) {
        char t[16];
        up2(t, "CONNECTED", sizeof(t));
        float wv = tWidth(F_MONO_400_9_5, t, lv);
        tDraw(F_MONO_400_9_5, t, 379 - wv, wv, txtTop + nmLine2 / 2 - 4,
              C(C_ACCENT), 0, lv, 0, 1);
      } else if (nw.five) {
        char t[16];
        up2(t, "5 GHz", sizeof(t));
        float wv = tWidth(F_MONO_400_9_5, t, lv);
        tDraw(F_MONO_400_9_5, t, 379 - wv, wv, txtTop + nmLine2 / 2 - 4,
              C(C_LIVE), 0, lv, 0, 1);
      } else if (nw.open) {
        char t[16];
        up2(t, "OPEN", sizeof(t));
        float wv = tWidth(F_MONO_400_9_5, t, lv);
        tDraw(F_MONO_400_9_5, t, 379 - wv, wv, txtTop + nmLine2 / 2 - 4,
              C(C_INK2), 0, lv, 0, 1);
      } else {
        icLock(379 - 11, ry + 21, C(C_LOCK), 11.0f / 12);
      }
    }
    listChrome(cv, 178, 202, wlistY, contentH);
  }
}

/* =================================================================== */
void SigUI::paintPass() {
  cv->fillScreen(0x0000);
  icX(388, 114, C_INK2, 5.5f);

  /* ssid (+ lock when secured) */
  float lsn = 16 * 0.0f;
  float wt = tWidth(F_DISP_600_16, passSsid.c_str(), lsn);
  float lockW = passOpen ? 0 : 15;
  float x0 = (466 - (lockW + wt)) / 2;
  if (!passOpen) icLock(x0, 111, C_INK, 0.92f);
  tDraw(F_DISP_600_16, passSsid.c_str(), x0 + lockW, wt, 108, C_INK, 0, lsn,
        0, 1);

  /* box */
  cv->fillRoundRect(78, 148, 310, 46, 12, H565(0x0F0F0F));
  cv->drawRoundRect(78, 148, 310, 46, 12, H565(0x242424));
  /* value: revealed text while peeked (held) or shown (eye toggle),
   * else solid dots (filled circles — the • glyph lit only ~7px/dot
   * and read as "nothing appears"). Content area [78..344] leaves the
   * eye button zone [344..388] clear, matching the prototype's
   * 44px padding-right. Long values ellipsize like the CSS does. */
  bool reveal = passShow || passPeek;
  const float cx0 = 78, cw = 266;
  float wd, dx;
  if (!reveal) {
    size_t n = MIN((size_t)32, passVal.length());
    const float sp = 13, dotR = 3;
    wd = n ? (dotR * 2 + (n - 1) * sp) : 0;
    dx = cx0 + (cw - (wd + 3 + 2)) / 2;
    for (size_t i = 0; i < n; i++)
      fillC((int)(dx + dotR + i * sp), 171, dotR, C_INK);
  } else {
    float avail = cw - 8;
    wd = tWidth(F_DISP_600_16, passVal.c_str(), 0);
    if (wd > avail) wd = avail;
    dx = cx0 + (cw - (wd + 3 + 2)) / 2;
    tDraw(F_DISP_600_16, passVal.c_str(), dx, avail, 163, C_INK, 0, 0, 0, 1);
  }
  /* caret: 1s blink */
  if (fmodf(millis() / 1000.0f, 1.0f) < 0.6f)
    cv->fillRect((int)(dx + wd + 3), 162, 2, 18, C_ACCENT);

  /* eye: slashed+gray while hidden, open+accent while revealed */
  icEye(366, 171, passShow ? C_ACCENT : C_INK2, !passShow);

  /* error */
  if (passErr) {
    const char* e = "Password must be at least 8 characters";
    float wv = tWidth(F_MONO_400_10, e, 0);
    tDraw(F_MONO_400_10, e, 233 - wv / 2, wv, 204, C_LIVE, 0, 0, 0, 1);
  }

  /* keyboard */
  for (int row = 0; row < kbRows(); row++) {
    int keyTop = 228 + row * 46;
    int nk = kbKeys(row);
    for (int c = 0; c < nk; c++) {
      int kx = kbKeyX(row, c), kw = kbKeyW(row, c);
      const char* dat = kbData(row, c);
      const char* lab = kbDisp(row, c);
      bool isShift = !strcmp(dat, "SHIFT");
      bool isBk = !strcmp(dat, "BK");
      bool isMode = !strcmp(dat, "MODE");
      bool isSpace = !strcmp(dat, "SPACE");
      bool isJoin = !strcmp(dat, "JOIN");
      if (isJoin) {
        cv->fillRoundRect(kx, keyTop, kw, 38, 8, C_ACCENT);
        float lb = 11 * 0.12f;
        float wl = tWidth(F_MONO_600_11, "JOIN", lb);
        tDraw(F_MONO_600_11, "JOIN", kx + (kw - wl) / 2, wl,
              keyTop + (38 - 11) / 2.0f, H565(0x0B0B0C), 0, lb, 0, 1);
        continue;
      }
      uint16_t bg = (isShift || isBk || isMode || isSpace) ? C_KEYMOD
                                                           : C_KEYBG;
      if (kbPr == row * 16 + c) bg = H565(0x23232A);  /* .key:active */
      cv->fillRoundRect(kx, keyTop, kw, 38, 8, bg);
      cv->drawRoundRect(kx, keyTop, kw, 38, 8, C_LINE);
      if (isShift) {
        icShift(kx + kw / 2.0f, keyTop + 19,
                kbCaps ? C_ACCENT : C_INK2);
      } else if (isBk) {
        icBksp(kx + kw / 2.0f, keyTop + 19, C_INK2);
      } else if (isMode) {
        float lb = 10 * 0.06f;
        float wl = tWidth(F_MONO_400_11, lab, lb);
        tDraw(F_MONO_400_11, lab, kx + (kw - wl) / 2, wl, keyTop + 14,
              C_INK2, 0, lb, 0, 1);
      } else if (isSpace) {
        float lb = 9 * 0.2f;
        float wl = tWidth(F_MONO_400_9_5, "space", lb);
        tDraw(F_MONO_400_9_5, "space", kx + (kw - wl) / 2, wl,
              keyTop + 14.5f, C_KEYSPC, 0, lb, 0, 1);
      } else {
        float wl = tWidth(F_DISP_500_15, lab, 0);
        tDraw(F_DISP_500_15, lab, kx + (kw - wl) / 2, wl, keyTop + 11.5f,
              C_INK, 0, 0, 0, 1);
      }
    }
  }
}

/* =================================================================== */
void SigUI::paintNet() {
  cv->fillScreen(0x0000);
  int y = NET_TOP;
  cv->fillRoundRect(NET_X, y, NET_W, NET_H, 20, C_CARDBG);
  cv->drawRoundRect(NET_X, y, NET_W, NET_H, 20, C_CARDLN);
  icX(357, y + 32, C_INK2, 5.5f);                 /* 40px btn: 337..377 */

  /* shield check */
  icShield(233, y + 26 + 17, C_ACCENT);

  /* ssid + meta */
  auto& rr = wcResults();
  String ssid = (netIdx >= 0 && netIdx < (int)rr.size()) ? rr[netIdx].ssid
                                                         : wcSsid();
  bool hot = (netIdx >= 0 && netIdx < (int)rr.size()) && rr[netIdx].hot;
  bool open = (netIdx >= 0 && netIdx < (int)rr.size()) && rr[netIdx].open;
  float wn = tWidth(F_DISP_700_21, ssid.c_str(), 0);
  tDraw(F_DISP_700_21, ssid.c_str(), 233 - wn / 2, wn, 178, C_INK, 0, 0, 0,
        1);
  float l21 = font(F_DISP_700_21)->ascent + font(F_DISP_700_21)->descent +
              font(F_DISP_700_21)->gap;
  char meta[72];
  snprintf(meta, sizeof(meta), "Connected \xc2\xb7 %s \xc2\xb7 %s",
           open ? "Open" : "WPA2", hot ? "phone hotspot" : "2.4 GHz");
  char mu[80];
  up2(mu, meta, sizeof(mu));
  float lm = 10 * 0.14f;
  float wm = tWidth(F_MONO_400_10, mu, lm);
  tDraw(F_MONO_400_10, mu, 233 - wm / 2, wm, 178 + l21 + 8, C_INK2, 0, lm,
        0, 1);

  /* actions */
  float lb = 11 * 0.14f;
  cv->fillRoundRect(101, NET_ACTS_Y, 127, 46, 23, C_ACCENT);
  char d[16];
  up2(d, "DISCONNECT", sizeof(d));
  float wd = tWidth(F_MONO_600_11, d, lb);
  tDraw(F_MONO_600_11, d, 101 + (127 - wd) / 2, wd, NET_ACTS_Y + 17.5f,
        H565(0x0B0B0C), 0, lb, 0, 1);
  cv->drawRoundRect(238, NET_ACTS_Y, 127, 46, 23, H565(0x743A37));
  char fg[16];
  up2(fg, "FORGET", sizeof(fg));
  float wf = tWidth(F_MONO_600_11, fg, lb);
  tDraw(F_MONO_600_11, fg, 238 + (127 - wf) / 2, wf, NET_ACTS_Y + 17.5f,
        C_FG, 0, lb, 0, 1);

  /* hint: greedy wrap to 2 lines within 264 px */
  const char* words[] = {"Disconnect", "keeps", "the", "password", "\xe2\x80\x94",
                         "forget", "erases", "it."};
  float lh = 9.5f * 1.7f;
  float lsh = 9.5f * 0.05f;
  char line1[80] = "", line2[80] = "";
  float maxW = 264, curW = 0;
  bool second = false;
  for (auto wd2 : words) {
    float ww = tWidth(F_MONO_400_9_5, wd2, lsh);
    float add = ww + (curW > 0 ? tWidth(F_MONO_400_9_5, " ", lsh) : 0);
    char* dst = second ? line2 : line1;
    if (curW + add > maxW && curW > 0 && !second) {
      second = true;
      dst = line2;
      curW = 0;
      add = ww;
      strcat(line2, wd2);
      curW = ww;
      continue;
    }
    if (dst[0]) strcat(dst, " ");
    strcat(dst, wd2);
    curW += add;
  }
  float hy = NET_ACTS_Y + 46 + 14;
  if (line1[0]) {
    float wl = tWidth(F_MONO_400_9_5, line1, lsh);
    tDraw(F_MONO_400_9_5, line1, 233 - wl / 2, wl, hy, C_HINT, 0, lsh, 0, 1);
  }
  if (line2[0]) {
    float wl = tWidth(F_MONO_400_9_5, line2, lsh);
    tDraw(F_MONO_400_9_5, line2, 233 - wl / 2, wl, hy + lh, C_HINT, 0, lsh,
          0, 1);
  }
}

void SigUI::paintToast() {
  float ls = 10 * 0.06f;
  float wt = tWidth(F_MONO_400_10, toastMsg.c_str(), ls);
  float w = 32 + wt;
  float h = 18 + (font(F_MONO_400_10)->ascent + font(F_MONO_400_10)->descent +
                  font(F_MONO_400_10)->gap);
  float x = (466 - w) / 2, y = 466 - 58 - h;
  cv->fillRoundRect((int)x, (int)y, (int)w, (int)h, 11, C_TOASTBG);
  cv->drawRoundRect((int)x, (int)y, (int)w, (int)h, 11, C_CARDLN);
  tDraw(F_MONO_400_10, toastMsg.c_str(), x + 16, wt, y + 9, C_INK, 0, ls, 0,
        1);
}
