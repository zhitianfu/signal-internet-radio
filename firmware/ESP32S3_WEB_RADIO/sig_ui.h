#pragma once
/* Signal UI — firmware port of the 004-signal high-fidelity prototype.
 * Pixel-for-pixel layout from the prototype CSS; behavior from its JS.
 * Rendering goes through an offscreen RGB565 canvas (PSRAM) so blended
 * text and buffer-level effects (swipe offset, list fades) stay cheap. */

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <vector>
#include "display.h"
#include "touch.h"
#include "audio.h"
#include "station_manager.h"
#include "wifi_client.h"

#include "sig/signal_fonts.h"          /* types + font ids (no data) */

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/* palette (theme on-air) */
#define H565(h) ((uint16_t)( (((h) >> 16 & 0xF8) << 8) | \
                            (((h) >>  8 & 0xFC) << 3) | \
                            (((h) >>  3) & 0x1F) ))
#define C_ACCENT   H565(0xFFB020)
#define C_LIVE     H565(0xFF3B30)
#define C_LIVESOFT H565(0x210706)
#define C_LIVELINE H565(0x7C1C17)
#define C_INK      H565(0xF4F2ED)
#define C_INK2     H565(0x96938B)
#define C_SURF     H565(0x141417)
#define C_LINE     H565(0x171717)
#define C_WHITE    0xFFFF
#define C_STATUS   H565(0xB9B6AE)
#define C_PAUSEBG  H565(0x0D0D0D)
#define C_PAUSELN  H565(0x212121)
#define C_PAUSE    H565(0x8B887F)
#define C_PROG     H565(0xEDEAE4)
#define C_SEC      H565(0x66645D)
#define C_SUB      H565(0x8B887F)
#define C_MONO40   H565(0xA9A69E)
#define C_ICON     H565(0xDAD7D0)
#define C_KEYBG    H565(0x17171C)
#define C_KEYMOD   H565(0x202027)
#define C_KEYSPC   H565(0x6E6C65)
#define C_CARDBG   H565(0x101013)
#define C_CARDLN   H565(0x242424)
#define C_FG       H565(0xE8756E)
#define C_HINT     H565(0x6E6C65)
#define C_CONHINT  H565(0x605E57)
#define C_LOCK     H565(0x7E7B73)
#define C_TOASTBG  H565(0x1B1B20)
#define C_OFFIC    H565(0x5F5D57)
#define C_SECOFF   H565(0x2E2E2E)

/* netcard fixed layout (content height is fixed: hint wraps to 2 lines) */
#define NET_H       254
#define NET_TOP     (233 - NET_H / 2)        /* 106 */
#define NET_ACTS_Y  (NET_TOP + 138)          /* 244 */
#define NET_X       77
#define NET_W       312

enum UiView : uint8_t { V_BOOT, V_STANDBY, V_NOW, V_STATIONS, V_SETTINGS, V_WIFI };
enum UiOvl  : uint8_t { O_NONE, O_CON, O_ERR, O_VOL, O_PASS, O_NET };

/* drag modes */
enum : uint8_t { DRAG_NONE, DRAG_DIAL, DRAG_V, DRAG_LIST, DRAG_VOL, DRAG_BR };

class SigUI {
public:
  /* display extras per station (public: shared by painters/layout) */
  struct SDef { const char* code; const char* l1; const char* l2;
                const char* prog; const char* sec; };
  static const SDef SD[9];

  /* font table lives in sig_ui.cpp (the only TU with glyph data) */
  static const sig_font_t* font(int i);

  void begin(DisplayUI* d, StreamPlayer* p, StationManager* s);
  void pump();                       // input + timers + render (every loop)

  /* hardware button hooks (BOOT button / long-press) */
  void btnNext();                    // next station (legacy BOOT behavior)
  void toggleStandby();              // long-press: PWR-style standby

private:
  /* ---- lifecycle / dispatch ---- */
  void render();
  void console();                     // serial commands (D=dump, T=touch, P=state)
  void dumpFrame();                   // 466x466 PPM over USB CDC
  void paintView();               // current view only (no dial)
  void tickTimers();
  void onTouch();
  void setView(uint8_t v);
  void setOvl(uint8_t o);

  /* ---- state-machine actions ---- */
  void selectStation(int i, bool connect = true);
  void stepStation(int d);
  void startConnect();
  void showError();
  void cancelConnect();
  void openVol();  void hideVol(); void setVol(int v);
  void enterWifi(); void doScan();
  void handleNet(int idx);
  void wifiJoin(const String& ssid, const String& pass);
  void openPass(int idx); void closePass();
  void keyPress(const char* k);
  void tryJoin();
  void openNet(int idx);
  void dropConnection(bool forget);
  void toast(const char* msg);
  void setWifiPower(bool on);
  void replayBoot();
  void armSleep(); void checkSleep();
  void saveState(); void loadState();

  /* ---- hit testing (role codes) ---- */
  int hitNow(float x, float y);
  int hitListHead(float x, float y);          // back / gear / rescan
  int hitStationRow(float y, float* outScroll);
  int hitSettings(float x, float y);
  int hitWifi(float x, float y);              // header + wrow + wbtn + rows
  int hitPassKey(float x, float y);           // returns key index or -1
  int hitErr(float x, float y);
  int hitNet(float x, float y);

  /* ---- painters (sig_paint.cpp) ---- */
  void paintBoot();
  void paintStandby();
  void paintNow();
  void paintDial(bool full);
  void paintStations();
  void paintSettings();
  void paintCon();
  void paintErr();
  void paintVol();
  /* ---- painters (sig_wifi.cpp) ---- */
  void paintWifiView();
  void paintPass();
  void paintNet();
  void paintToast();

  /* ---- drawing primitives (sig_ui.cpp) ---- */
  void fillR(int x, int y, int w, int h, int r, uint16_t c);
  void strokeR(int x, int y, int w, int h, int r, uint16_t c);
  void fillC(int cx, int cy, int r, uint16_t c);
  void ringC(int cx, int cy, int r, uint16_t c, float a0 = 0, float a1 = 360,
             float th = 1);
  void thickLine(float x1, float y1, float x2, float y2, float w, uint16_t c);
  uint16_t blendC(uint16_t dst, uint16_t src, uint8_t a15);
  void setAlpha(uint8_t a) { _ga = a; }       // global text/shape alpha 0..15

  /* text engine: CSS-accurate (baseline from font metrics + line box) */
  float tWidth(int fi, const char* s, float ls, float scale = 1);
  /* align: 0 left, 1 center, 2 right within [x, x+boxW]; lh: 0 = normal
   * line-height, else multiplier of font size; bg = backdrop color under
   * glyphs (0 for pure black), maxW 0 = boxW; ellip: truncate + ellipsis. */
  void tDraw(int fi, const char* s, float x, float boxW, float cssTop,
             uint16_t col, uint8_t align, float ls, uint16_t bg = 0,
             float lh = 0, float scale = 1, bool ellip = true,
             float maxW = 0);
  void tDrawC(int fi, const char* s, float cx, float cssTop, uint16_t col,
              float ls, uint16_t bg = 0, float lh = 0);   // centered helper
  void initFonts();

  /* vector icons (exact SVG geometry from the prototype markup) */
  void icWifiBars(float x, float y, uint16_t c, float thirdA = 15);
  void icBattery(float x, float y, uint16_t c);
  void icList(float cx, float cy, uint16_t c);
  void icPrev(float cx, float cy, uint16_t c);
  void icPlay(float cx, float cy, uint16_t c);
  void icPause(float cx, float cy, uint16_t c);
  void icNext(float cx, float cy, uint16_t c);
  void icVol(float cx, float y, uint16_t c);
  void icBack(float cx, float cy, uint16_t c);
  void icGear(float cx, float cy, uint16_t c);
  void icRescan(float cx, float cy, uint16_t c, float rot);
  void icLock(float x, float y, uint16_t c, float s = 1);
  void icShield(float cx, float cy, uint16_t c);
  void icX(float cx, float cy, uint16_t c, float r);
  void icEye(float cx, float cy, uint16_t c, bool off);
  void icChevron(float cx, float cy, uint16_t c);
  void icWifiOff(float cx, float cy, uint16_t c);
  void icShift(float cx, float cy, uint16_t c);
  void icBksp(float cx, float cy, uint16_t c);
  void icArrowR(float cx, float cy, uint16_t c, float sz);   // for "→"

  /* ---- members ---- */
  DisplayUI* disp = nullptr;
  StreamPlayer* pl = nullptr;
  StationManager* sm = nullptr;
  Arduino_GFX* g = nullptr;
  Arduino_Canvas* cv = nullptr;
  Arduino_Canvas* vcv = nullptr;   // view layer for swipe follow

  bool dirty = true;
  static bool s_showXh;          // crosshair overlay toggle (console 'X')
  uint8_t _ga = 15;                              // global alpha

  /* view / overlay */
  uint8_t view = V_BOOT, ovl = O_NONE;
  int cur = 0;
  float rot = 0, rotFrom = 0, rotTo = 0;
  bool rotAnim = false; uint32_t rotT0 = 0;
  int lastNear = -1;
  int pendingSel = -1; uint32_t pendingT0 = 0;   // dial release -> select
  float viewShiftY = 0;                          // vertical swipe follow
  bool playing = false, connecting = false;
  uint32_t conT0 = 0; bool conForWifi = false;
  uint8_t vol = 62; float bright = 1.0f;
  uint8_t sleepIdx = 0;                          // 0 OFF 1=15 2=30 3=60
  bool sleepArmed = false; uint32_t sleepT0 = 0;

  /* timers */
  uint32_t bootT0 = 0, waveT0 = 0, ripT0 = 0;
  uint32_t volHideT0 = 0;
  bool scanDone = false;
  int ripX = 0, ripY = 0;
  uint32_t animT0 = 0;                           // next ambient repaint

  /* wifi view */
  uint8_t warea = 0;                             // 0 scan 1 off 2 list
  float listY = 0, wlistY = 0;
  uint32_t scanT0 = 0; bool scanShown = false;

  /* pass keyboard */
  int passIdx = -1; String passSsid; bool passOpen = false;
  String passVal; bool kbCaps = false, kbNum = false, passErr = false;
  bool passShow = false, passPeek = false;   // eye toggle / press-hold peek
  /* netcard */
  int netIdx = -1;
  /* toast */
  String toastMsg; uint32_t toastT0 = 0;

  /* input */
  bool pressing = false;
  uint8_t pressView = V_BOOT;                    // view at pointer-down
  float sx = 0, sy = 0, cx = 0, cy = 0;
  uint8_t dragMode = DRAG_NONE;
  float rotStart = 0, pressAng = 0;
  int lpRow = -1; float lpLast = 0; int lpMoved = 0;
  bool volIn = false;
  int brDrag = -1;                               // 1 = tracking brightness

  /* shared button rects (paint + hit test): [0]=retry/[1]=skip; won = Turn-on */
  void errBtnRects(int r[2][4]);
  void wonBtnRect(int r[4]);

  /* shared keyboard layout (paint + hit test) */
  int  kbRows() const;                           // 4
  int  kbKeys(int row) const;
  int  kbKeyX(int row, int col) const;           // absolute x
  int  kbKeyW(int row, int col) const;
  const char* kbData(int row, int col);   // raw key identity (dispatch)
  const char* kbDisp(int row, int col);   // display label (prototype rules)
  int kbRowCol(int x, int y);             // grid cell for press feedback
  int8_t kbPr = -1;                       // pressed cell (row*16+col) or -1

  /* font glyph index (byte offsets into each font's chars string) */
  static const int NF = 31;
  static const int NG = 128;
  uint16_t chOff[NF][NG];
  uint16_t chLen[NF][NG];

};

extern SigUI sigui;
