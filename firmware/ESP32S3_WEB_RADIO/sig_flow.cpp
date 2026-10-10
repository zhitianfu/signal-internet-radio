/* Signal UI flow: state-machine actions + touch input + hit testing. */
#include "sig_ui.h"
#include <math.h>

static inline float angOf(float x, float y) {
  float a = atan2f(x - 233, -(y - 233)) * RAD_TO_DEG;
  if (a < 0) a += 360;
  return a;
}
static inline float distC(float x, float y) {
  return hypotf(x - 233, y - 233);
}
static inline bool inRect(float x, float y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}
static inline bool inCirc(float x, float y, float cx, float cy, float r) {
  return hypotf(x - cx, y - cy) <= r;
}

/* button roles (now screen) */
enum { R_NONE, R_LIST, R_PREV, R_PLAY, R_NEXT, R_VOL };
/* header roles */
enum { H_NONE, H_BACK, H_GEAR };
/* wifi roles */
enum { W_NONE, W_BACK, W_RESCAN, W_WROW, W_WON };

/* =================================================================== */
void SigUI::setView(uint8_t v) {
  if (v > V_WIFI) return;
  view = v;
  /* listY is shared by the stations and settings lists — entering either
   * view starts at the top (a scrolled stations list used to push every
   * settings row off-screen: empty SETTINGS body, unreachable Wi-Fi row) */
  if (v == V_STATIONS || v == V_SETTINGS) listY = 0;
  if (v == V_WIFI) enterWifi();
  if (v != V_NOW) hideVol();
  dragMode = DRAG_NONE;
  viewShiftY = 0;
  dirty = true;
}

void SigUI::setOvl(uint8_t o) { ovl = o; dirty = true; }

void SigUI::selectStation(int i, bool connect) {
  cur = ((i % 9) + 9) % 9;
  /* dial animation (520 ms ease-out) */
  rotFrom = rot;
  rotTo = -cur * 40.0f;
  rotT0 = millis();
  rotAnim = true;
  lastNear = cur;
  saveState();
  if (connect && view == V_NOW) startConnect();
  dirty = true;
}

void SigUI::stepStation(int d) {
  Serial.printf("[act] stepStation %d: %d->%d\n", d, cur, cur + d);
  selectStation(cur + d);
}

void SigUI::startConnect() {
  hideVol();
  setOvl(O_NONE);
  conForWifi = false;
  conT0 = millis();
  connecting = true;
  Serial.printf("[act] startConnect cur=%d ovl->O_CON\n", cur);
  playing = false;
  ovl = O_CON;
  dirty = true;
  RadioStation s = sm->getStation(cur);
  pl->startStream(s.url.c_str());
}

void SigUI::showError() {
  connecting = false;
  playing = false;
  pl->stopStream();
  ovl = O_ERR;
  dirty = true;
  Serial.printf("[act] showError cur=%d\n", cur);
}

void SigUI::cancelConnect() {
  connecting = false;
  playing = false;
  pl->stopStream();
  setOvl(O_NONE);
}

void SigUI::openVol() {
  ovl = O_VOL;
  volHideT0 = millis();
  dirty = true;
}
void SigUI::hideVol() {
  if (ovl == O_VOL) setOvl(O_NONE);
}
void SigUI::setVol(int v) {
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  if ((uint8_t)v != vol) {
    vol = v;
    pl->setVolume(vol);
    saveState();
  }
  volHideT0 = millis();
  dirty = true;
}

/* ---------------- wifi view ---------------- */
void SigUI::enterWifi() {
  warea = wcPowered() ? 0 : 1;
  scanT0 = millis();
  scanDone = false;
  wlistY = 0;
  if (wcPowered()) wcScanAsync();
  dirty = true;
}

void SigUI::doScan() {
  if (!wcPowered()) { toast("Turn Wi-Fi on first"); return; }
  warea = 0;
  scanT0 = millis();
  scanDone = false;
  wcScanAsync();
  dirty = true;
}

void SigUI::handleNet(int idx) {
  auto& nets = wcResults();
  if (idx < 0 || idx >= (int)nets.size()) return;
  WcNet n = nets[idx];
  if (n.five) { toast("ESP32-S3 supports 2.4 GHz only"); return; }
  if (wcConnected() && n.ssid == wcSsid()) { openNet(idx); return; }
  if (wcHasSaved(n.ssid)) { wifiJoin(n.ssid, wcPassFor(n.ssid)); return; }
  if (n.open) { wifiJoin(n.ssid, ""); return; }
  openPass(idx);
}

void SigUI::wifiJoin(const String& ssid, const String& pass) {
  passSsid = ssid;
  conForWifi = true;
  conT0 = millis();
  connecting = true;
  ovl = O_CON;
  wcJoinAsync(ssid, pass);
  dirty = true;
}

void SigUI::openPass(int idx) {
  auto& nets = wcResults();
  if (idx < 0 || idx >= (int)nets.size()) return;
  passIdx = idx;
  passSsid = nets[idx].ssid;
  passOpen = nets[idx].open;
  passVal = "";
  kbCaps = false;
  kbNum = false;
  passErr = false;
  passShow = false;
  passPeek = false;
  setOvl(O_PASS);
}
void SigUI::closePass() {
  setOvl(O_NONE);
  passIdx = -1;
}

void SigUI::keyPress(const char* k) {
  /* trace: key label + mode + buffer LENGTH only (never the password) */
  Serial.printf("[key] '%s' num=%d caps=%d len=%d\n", k, (int)kbNum,
                (int)kbCaps, (int)passVal.length());
  if (!strcmp(k, "SHIFT")) kbCaps = !kbCaps;
  else if (!strcmp(k, "MODE")) kbNum = !kbNum;
  else if (!strcmp(k, "BK")) {
    if (passVal.length()) passVal.remove(passVal.length() - 1);
  } else if (!strcmp(k, "SPACE")) passVal += ' ';
  else if (!strcmp(k, "JOIN")) { tryJoin(); return; }
  else {
    if (!kbNum && kbCaps && strlen(k) == 1 && k[0] >= 'a' && k[0] <= 'z') {
      char up[2] = {(char)(k[0] - 32), 0};
      passVal += up;
    } else passVal += k;
  }
  passErr = false;
  dirty = true;
}

void SigUI::tryJoin() {
  String t = passVal;
  t.trim();
  if (t.length() < 8) { passErr = true; dirty = true; return; }
  String ssid = passSsid, pass = passVal;
  closePass();
  wifiJoin(ssid, pass);
}

void SigUI::openNet(int idx) {
  netIdx = idx;
  setOvl(O_NET);
}

void SigUI::dropConnection(bool forget) {
  String was = wcSsid();
  if (forget) wcForget(was);
  wcDisconnect();
  playing = false;
  pl->stopStream();
  setOvl(O_NONE);
  { String m = String(forget ? "FORGOT" : "DISCONNECTED") + " \xc2\xb7 " +
               was;
    toast(m.c_str()); }
}

void SigUI::toast(const char* msg) {
  toastMsg = msg;
  toastT0 = millis();
  dirty = true;
}

void SigUI::setWifiPower(bool on) {
  wcSetPower(on);
  if (!on) {
    warea = 1;
    if (playing) { playing = false; pl->stopStream(); }
  } else {
    doScan();
  }
  dirty = true;
}

void SigUI::replayBoot() {
  bootT0 = millis();
  view = V_BOOT;
  ovl = O_NONE;
  connecting = false;
  playing = false;
  pendingSel = -1;
  rot = -cur * 40.0f;
  rotAnim = false;
  pl->stopStream();
  dirty = true;
}

void SigUI::btnNext() { stepStation(1); }

void SigUI::toggleStandby() {
  setView(view == V_STANDBY ? V_NOW : V_STANDBY);
}

void SigUI::armSleep() {
  static const uint32_t mins[4] = {0, 15, 30, 60};
  if (sleepIdx == 0) sleepArmed = false;
  else { sleepT0 = millis() + mins[sleepIdx] * 60000UL; sleepArmed = true; }
}

/* =================================================================== */
/* shared button rects                                                 */
/* =================================================================== */
void SigUI::errBtnRects(int r[2][4]) {
  float ls = 11.5f * 0.16f;
  int rw = 48 + (int)tWidth(F_MONO_600_11_5, "RETRY", ls);
  int sw = 48 + (int)tWidth(F_MONO_600_11_5, "NEXT STATION", ls);
  int total = rw + 12 + sw;
  int x0 = (466 - total) / 2;
  r[0][0] = x0; r[0][1] = 264; r[0][2] = rw; r[0][3] = 46;
  r[1][0] = x0 + rw + 12; r[1][1] = 264; r[1][2] = sw; r[1][3] = 46;
}

void SigUI::wonBtnRect(int r[4]) {
  float ls = 11.0f * 0.16f;
  int w = 52 + (int)tWidth(F_MONO_600_11, "TURN ON", ls);
  float wtLine = font(F_MONO_400_11)->px;                 /* ~11..13 line */
  float contentH = 28 + 16 + wtLine + 18 + 44;
  float y0 = 178 + (202 - contentH) / 2;
  r[0] = 233 - w / 2;
  r[1] = (int)(y0 + 28 + 16 + wtLine + 18);
  r[2] = w;
  r[3] = 44;
}

/* =================================================================== */
/* keyboard layout (shared)                                            */
/* =================================================================== */
int SigUI::kbRows() const { return 4; }

int SigUI::kbKeys(int row) const {
  if (!kbNum) return row == 0 ? 10 : (row == 1 ? 9 : (row == 2 ? 9 : 3));
  return row == 0 ? 10 : (row == 1 ? 9 : (row == 2 ? 7 : 3));
}

int SigUI::kbKeyX(int row, int col) const {
  /* row start x: letters [36,56,44,87] / numbers [36,56,87,87] */
  static const int startA[4] = {36, 56, 44, 87};
  static const int startN[4] = {36, 56, 87, 87};
  int x = kbNum ? startN[row] : startA[row];
  for (int c = 0; c < col; c++) {
    bool wide = false;
    if (!kbNum && row == 2 && c == 0) wide = true;             // SHIFT
    if (!kbNum && row == 2 && c == 8) wide = true;             // BK
    if (kbNum && row == 2 && c == 6) wide = true;              // BK
    bool mode = (row == 3 && c == 0);
    bool space = (row == 3 && c == 1);
    bool join = (row == 3 && c == 2);
    int w = wide ? 46 : (mode ? 50 : (space ? 144 : (join ? 86 : 34)));
    x += w + 6;
  }
  return x;
}

int SigUI::kbKeyW(int row, int col) const {
  if (row == 3) return col == 0 ? 50 : (col == 1 ? 144 : 86);
  if (!kbNum && row == 2 && (col == 0 || col == 8)) return 46;
  if (kbNum && row == 2 && col == 6) return 46;
  return 34;
}

/* RAW key identity - what dispatch sees (prototype's data-k).
 * Never transformed: keyPress() owns the caps/case rule. */
const char* SigUI::kbData(int row, int col) {
  static const char* A[4][11] = {
    {"q","w","e","r","t","y","u","i","o","p",nullptr},
    {"a","s","d","f","g","h","j","k","l",nullptr,nullptr},
    {"SHIFT","z","x","c","v","b","n","m","BK",nullptr,nullptr},
    {"MODE","SPACE","JOIN",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr},
  };
  static const char* N[4][11] = {
    {"1","2","3","4","5","6","7","8","9","0",nullptr},
    {"-","/",";","(",")","$","&","@\"",nullptr,nullptr},
    {".",",","?","!","\"","'","BK",nullptr,nullptr,nullptr,nullptr},
    {"MODE","SPACE","JOIN",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr},
  };
  const char* s = (kbNum ? N : A)[row][col];
  return s ? s : "";
}

/* DISPLAY label - prototype renderKb() rules: MODE shows 123/ABC,
 * letters upper only while SHIFT is on (and not in number layer),
 * space stays lowercase 'space'. */
const char* SigUI::kbDisp(int row, int col) {
  const char* s = kbData(row, col);
  if (!strcmp(s, "MODE")) return kbNum ? "ABC" : "123";
  if (strlen(s) == 1 && s[0] >= 'a' && s[0] <= 'z' && !kbNum && kbCaps) {
    static char up[2];
    up[0] = s[0] - 32; up[1] = 0;
    return up;
  }
  return s;
}

/* grid cell under (x,y) for press feedback: row*16+col, -1 outside */
int SigUI::kbRowCol(int x, int y) {
  if (y < 228 || y >= 228 + 4 * 46) return -1;
  int row = (y - 228) / 46;
  if ((y - 228) - row * 46 >= 38) return -1;
  int n = kbKeys(row);
  for (int c = 0; c < n; c++) {
    int kx = kbKeyX(row, c);
    if (x >= kx && x < kx + kbKeyW(row, c)) return row * 16 + c;
  }
  return -1;
}

/* kbKeyAt lives inline in onTouch (member access) */
/* keyboard hit logic is inlined in onTouch (member access) */
/* =================================================================== */
/* hit tests                                                           */
/* =================================================================== */
int SigUI::hitNow(float x, float y) {
  if (inCirc(x, y, 128.8f, 340.9f, 22)) return R_LIST;
  if (inCirc(x, y, 174.4f, 371.1f, 24)) return R_PREV;
  if (inCirc(x, y, 233, 383, 27)) return R_PLAY;
  if (inCirc(x, y, 291.6f, 371.1f, 24)) return R_NEXT;
  if (inCirc(x, y, 337.2f, 340.9f, 22)) return R_VOL;
  return R_NONE;
}

int SigUI::hitListHead(float x, float y) {
  if (inRect(x, y, 104, 70, 44, 44)) return H_BACK;
  if (view == V_STATIONS || view == V_WIFI)
    if (inRect(x, y, 318, 70, 44, 44)) return H_GEAR;
  return H_NONE;
}

/* settings: row index (0 wifi,1 bright,2 sleep,3 about) or -1 */
int SigUI::hitSettings(float x, float y) {
  if (!inRect(x, y, 73, 116, 320, 264)) return -1;
  float cy = y - 116 - listY;
  int i = (int)(cy / 77);
  if (i < 0 || i > 3) return -1;
  if (cy - i * 77 >= 64) return -1;
  if (!inRect(x, y, 73, 116, 320, 264)) return -1;
  return i;
}

int SigUI::hitWifi(float x, float y) {
  if (inRect(x, y, 104, 70, 44, 44)) return W_BACK;
  if (inRect(x, y, 318, 70, 44, 44)) return W_RESCAN;
  if (inRect(x, y, 73, 118, 320, 56)) return W_WROW;
  if (warea == 1) {
    int r[4]; wonBtnRect(r);
    if (inRect(x, y, r[0], r[1], r[2], r[3])) return W_WON;
  }
  return W_NONE;
}

int SigUI::hitErr(float x, float y) {
  int r[2][4];
  errBtnRects(r);
  if (inRect(x, y, r[0][0], r[0][1], r[0][2], r[0][3])) return 1;
  if (inRect(x, y, r[1][0], r[1][1], r[1][2], r[1][3])) return 2;
  return 0;
}

int SigUI::hitNet(float x, float y) {
  if (inRect(x, y, 337, NET_TOP + 12, 40, 40)) return 1;          // X
  if (inRect(x, y, 101, NET_ACTS_Y, 127, 46)) return 2;           // DISCONNECT
  if (inRect(x, y, 238, NET_ACTS_Y, 127, 46)) return 3;           // FORGET
  return 0;
}

/* =================================================================== */
/* input                                                               */
/* =================================================================== */
void SigUI::onTouch() {
  bool t = touch.isTouched();
  uint32_t now = millis();
  uint8_t ev = 0;
  uint16_t ex = 0, ey = 0;
  bool hasEv  = touch.popEvent(ev, ex, ey);
  bool doDown = hasEv && ev == 0;
  bool doUp   = hasEv && ev == 1;
  bool doMove = hasEv && ev == 2;   /* injected drag point (queue type 2) */

  if (doDown && !pressing) {
    /* ---------------- pointer down (from poll task queue) ----------
     * guard: a duplicate down event must NOT re-run this body — it
     * would reset sx/sy and dragMode mid-gesture and kill swipes */
    pressing = true;
    pressView = view;
    sx = cx = ex;
    sy = cy = ey;
    Serial.printf("[tapD] v=%u o=%u p=(%d,%d)\n", view, ovl, (int)sx, (int)sy);
    dragMode = DRAG_NONE;
    lpMoved = 0;
    lpLast = sy;
    lpRow = -1;
    viewShiftY = 0;
    volIn = false;
    brDrag = 0;

    /* keyboard press feedback (prototype .key:active) */
    if (ovl == O_PASS) {
      kbPr = kbRowCol((int)sx, (int)sy);
      /* peek: hold anywhere on the value field to reveal while held
       * (the eye-button zone is excluded — that one is a toggle) */
      bool pk = inRect(sx, sy, 78, 148, 266, 46);
      if (pk != passPeek) { passPeek = pk; dirty = true; }
      dirty = true;
    } else {
      kbPr = -1;
      passPeek = false;
    }

    if (ovl != O_NONE) {
      if (ovl == O_VOL) {
        float a = angOf(sx, sy), d = distC(sx, sy);
        if (a >= 110 && a <= 250 && d > 100 && d < 200) {
          volIn = true;
          dragMode = DRAG_VOL;
          setVol((int)((240 - constrain(a, 120.0f, 240.0f)) / 120 * 100));
        }
      }
      dirty = true;
      return;
    }

    if (view == V_STANDBY) {
      ripX = sx; ripY = sy; ripT0 = now;
      setView(V_NOW);
      return;
    }

    if (view == V_NOW) {
      if (hitNow(sx, sy) == R_NONE) {
        ripX = sx; ripY = sy; ripT0 = now;
      }
      dirty = true;
      return;
    }

    if (view == V_SETTINGS) {
      /* brightness track drag */
      float cy2 = sy - 116 - listY;
      int i = (int)(cy2 / 77);
      if (i == 1 && cy2 - 77 >= 0 && cy2 - 77 < 64) {
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", (int)roundf(bright * 100));
        float pctW = tWidth(F_MONO_400_11_5, pct, 11.5f * 0.08f);
        int tx = 379 - (int)pctW - 7 - 112;
        /* hit target = full row height, track x +/-8 .. right edge of the
         * value zone: the bare 7px strip is unhittable with a finger */
        if (sx >= tx - 8 && sx <= 385) {
          dragMode = DRAG_BR;
          brDrag = 1;
          float f = (sx - tx) / 112.0f;
          if (f < 0) f = 0;
          if (f > 1) f = 1;
          bright = 0.5f + f * 0.5f;
          disp->setBrightness((uint8_t)(bright * 255));
          saveState();
          dirty = true;
          return;
        }
      }
    }

    /* list drag targets (stations / settings / wifi) */
    dragMode = DRAG_LIST;
    if (view == V_STATIONS && sy >= 116 && sy <= 380) {
      float cyy = sy - 116 - listY;
      float yy = 0;
      for (int i = 0; i < 9; i++) {
        if (i == 0 || strcmp(SD[i].sec, SD[i - 1].sec) != 0) yy += 34;
        if (cyy >= yy && cyy < yy + 64) { lpRow = i; break; }
        yy += 64;
        if (i < 8 && strcmp(SD[i + 1].sec, SD[i].sec) == 0) yy += 13;
      }
    } else if (view == V_WIFI && sy >= 178 && sy <= 380 && warea == 2) {
      float cyy = sy - 178 - wlistY;
      int idx = (int)(cyy / 68);
      if (cyy - idx * 68 < 56 && idx >= 0 &&
          idx < (int)wcResults().size()) lpRow = idx;
    }
    return;
  }

  if ((t || doMove) && pressing && !doDown) {
    /* ---------------- pointer move ---------------- */
    if (doMove) { cx = ex; cy = ey; }          /* injected drag point */
    else { cx = touch.getX(); cy = touch.getY(); }
    float dx = cx - sx, dy = cy - sy;

    switch (dragMode) {
      case DRAG_LIST: {
        lpMoved = MAX(lpMoved, (int)fabsf(dy));
        if (lpMoved > 6) {
          float delta = cy - lpLast;
          lpLast = cy;
          if (view == V_WIFI) {
            float h = (float)(wcResults().size() * 56 +
                              MAX(0, (int)wcResults().size() - 1) * 12);
            float mn = MIN(0.0f, 202 - h);
            wlistY = constrain(wlistY + delta, mn, 0.0f);
          } else {
            float h = (view == V_STATIONS) ? 735.0f : 299.0f;
            float mn = MIN(0.0f, 264 - h);
            listY = constrain(listY + delta, mn, 0.0f);
          }
          dirty = true;
        }
        break;
      }
      case DRAG_BR: {
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", (int)roundf(bright * 100));
        float pctW = tWidth(F_MONO_400_11_5, pct, 11.5f * 0.08f);
        int tx = 379 - (int)pctW - 7 - 112;
        float f = (cx - tx) / 112.0f;
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        bright = 0.5f + f * 0.5f;
        disp->setBrightness((uint8_t)(bright * 255));
        saveState();
        dirty = true;
        break;
      }
      case DRAG_VOL: {
        float a = angOf(cx, cy), d = distC(cx, cy);
        if (a >= 110 && a <= 250 && d > 100 && d < 200)
          setVol((int)((240 - constrain(a, 120.0f, 240.0f)) / 120 * 100));
        break;
      }
      case DRAG_NONE:
        if (view == V_NOW && ovl == O_NONE &&
            hypotf(dx, dy) > 9) {
          dragMode = (fabsf(dx) > fabsf(dy) * 1.15f || distC(cx, cy) > 176)
                         ? DRAG_DIAL : DRAG_V;
          if (dragMode == DRAG_DIAL) {
            rotStart = rot;
            pressAng = atan2f(sx - 233, -(sy - 233)) * RAD_TO_DEG;
            rotAnim = false;
          }
        }
        break;
      case DRAG_DIAL: {
        float a1 = atan2f(cx - 233, -(cy - 233)) * RAD_TO_DEG;
        float dd = a1 - pressAng;
        if (dd > 180) dd -= 360;
        if (dd < -180) dd += 360;
        rot = rotStart + dd;
        dirty = true;
        break;
      }
      case DRAG_V: {
        viewShiftY = constrain(dy * 0.6f, -48.0f, 48.0f);
        dirty = true;
        break;
      }
    }
    return;
  }

  if (doUp && pressing) {
    /* ---------------- pointer up (from poll task queue) ------------
     * guard: stray/duplicate ups (not preceded by a down) are ignored */
    cx = ex;
    cy = ey;
    pressing = false;
    float dx = cx - sx, dy = cy - sy;
    float moved = hypotf(dx, dy);
    int traceHit = 0;
    if (ovl == O_ERR) {
      traceHit = hitErr(cx, cy);
      if (traceHit == 0) traceHit = -hitErr(sx, sy);
    } else if (view == V_NOW && ovl == O_NONE) traceHit = hitNow(cx, cy);
    Serial.printf("[tapU] v=%u o=%u p=(%d,%d) l=(%d,%d) m=%.1f r=%d\n",
                  view, ovl, (int)sx, (int)sy, (int)cx, (int)cy, moved, traceHit);
    uint8_t mode = dragMode;
    dragMode = DRAG_NONE;
    float syStart = sy;
    uint8_t syView = pressView;
    bool wasVol = volIn;
    (void)syStart;

    /* overlays */
    if (ovl != O_NONE) {
      switch (ovl) {
        case O_CON:
          cancelConnect();
          break;
        case O_ERR:
          if (moved < 20) {
            int r = hitErr(cx, cy);
            if (r == 0) r = hitErr(sx, sy);   // finger rolled off on release
            if (r == 1) startConnect();
            else if (r == 2) stepStation(1);
            else if (inRect(cx, cy, 70, 212, 326, 46) ||
                     inRect(sx, sy, 70, 212, 326, 46)) {
              /* the hint literally says "join one in Settings → Wi-Fi" —
               * make it tappable so first boot has a real path to the
               * Wi-Fi manager even with no network configured */
              setOvl(O_NONE);
              setView(V_WIFI);
            } else {
              /* tap anywhere else dismisses (same as prototype's
               * tap-to-cancel connecting overlay) → back to Now,
               * where stations/settings are reachable */
              setOvl(O_NONE);
              dirty = true;
            }
          }
          break;
        case O_VOL:
          if (!wasVol) hideVol();
          volIn = false;
          break;
        case O_PASS:
          if (kbPr != -1) { kbPr = -1; dirty = true; }
          if (passPeek) { passPeek = false; dirty = true; }   /* release */
          if (moved < 20) {
            if (inRect(cx, cy, 366, 92, 44, 44) ||
                inRect(sx, sy, 366, 92, 44, 44)) { closePass(); break; }
            /* eye button: toggle show/hide (peek reverts on release) */
            if (inRect(cx, cy, 344, 148, 44, 46) ||
                inRect(sx, sy, 344, 148, 44, 46)) {
              passShow = !passShow;
              dirty = true;
              Serial.printf("[peek] show=%d len=%u\n", (int)passShow,
                            (unsigned)passVal.length());
              break;
            }
            /* use the release point; if it fell off the key (finger roll),
             * fall back to the press-down point — same as the buttons */
            float hy = cy;
            if (!(hy >= 228 && hy < 228 + 4 * 46)) hy = sy;
            if (hy >= 228 && hy < 228 + 4 * 46) {
              int row = (int)((hy - 228) / 46);
              if ((hy - 228) - row * 46 < 38) {
                float hx = cx;
                int n = kbKeys(row);
                bool hit = false;
                for (int c = 0; c < n; c++) {
                  int kx = kbKeyX(row, c);
                  if (hx >= kx && hx < kx + kbKeyW(row, c)) {
                    const char* k = kbData(row, c);
                    if (k && *k) { keyPress(k); hit = true; }
                    break;
                  }
                }
                if (!hit) {                     /* x missed: try press-x */
                  hx = sx;
                  for (int c = 0; c < n; c++) {
                    int kx = kbKeyX(row, c);
                    if (hx >= kx && hx < kx + kbKeyW(row, c)) {
                      const char* k = kbData(row, c);
                      if (k && *k) keyPress(k);
                      break;
                    }
                  }
                }
              }
            }
          }
          break;
        case O_NET:
          if (moved < 8) {
            int r = hitNet(cx, cy);
            if (r == 1) setOvl(O_NONE);
            else if (r == 2) dropConnection(false);
            else if (r == 3) dropConnection(true);
          }
          break;
        default: break;
      }
      viewShiftY = 0;
      return;
    }

    if (syView == V_STANDBY) { viewShiftY = 0; return; }  /* consumed at down */

    if (syView == V_NOW) {
      if (mode == DRAG_DIAL) {
        int near = ((int)lroundf(-rot / 40.0f) % 9 + 9) % 9;
        rotFrom = rot;
        rotTo = -near * 40.0f;
        rotT0 = now;
        rotAnim = true;
        if (near != cur) {
          pendingSel = near;
          pendingT0 = now + 260;
        }
      } else if (mode == DRAG_V) {
        if (dy < -64) setView(V_STATIONS);
        else if (dy > 64) setView(V_SETTINGS);
      } else if (moved < 20) {
        int r = hitNow(cx, cy);
        if (r == R_NONE) r = hitNow(sx, sy);
        switch (r) {
          case R_LIST: setView(V_STATIONS); break;
          case R_PREV: stepStation(-1); break;
          case R_PLAY:
            if (connecting) break;
            if (!playing) startConnect();
            else { playing = false; pl->stopStream(); dirty = true; }
            break;
          case R_NEXT: stepStation(1); break;
          case R_VOL: openVol(); break;
        }
      }
      viewShiftY = 0;
      return;
    }

    /* list views */
    if (moved < 20) {
      if (syView == V_STATIONS) {
        int h = hitListHead(cx, cy);
        if (h == H_NONE) h = hitListHead(sx, sy);
        if (h == H_BACK) { setView(V_NOW); return; }
        if (h == H_GEAR) { setView(V_SETTINGS); return; }
        if (lpRow >= 0 && inRect(cx, cy, 73, 116, 320, 264)) {
          setView(V_NOW);
          selectStation(lpRow);
          return;
        }
      } else if (syView == V_SETTINGS) {
        if (hitListHead(cx, cy) == H_BACK) { setView(V_NOW); return; }
        int row = hitSettings(cx, cy);
        if (row == 0) setView(V_WIFI);
        else if (row == 2) {
          sleepIdx = (sleepIdx + 1) % 4;
          armSleep();
          saveState();
          dirty = true;
        }
      } else if (syView == V_WIFI) {
        int h = hitWifi(cx, cy);
        if (h == W_BACK) { setView(V_SETTINGS); return; }
        if (h == W_RESCAN) { doScan(); return; }
        if (h == W_WROW) { setWifiPower(!wcPowered()); return; }
        if (h == W_WON) { setWifiPower(true); return; }
        if (lpRow >= 0 && warea == 2 &&
            inRect(cx, cy, 73, 178, 320, 202)) {
          handleNet(lpRow);
          return;
        }
      }
    }
    viewShiftY = 0;
  }
}
