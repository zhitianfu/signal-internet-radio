#include "wifi_client.h"

/* NVS namespace/keys (same names the legacy wifi_manager uses) */
#define PREF_WIFI "wifi"
#define PREF_SSID "ssid"
#define PREF_PASS "pass"
#include "config.h"
#include "debug.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>

/* ---- credential store -------------------------------------------------
 * "ssid"/"pass"  = legacy single pair (kept for wifi_manager compat)
 * "creds"        = JSON map of every saved network
 * "pow"          = radio power state (1 = on)                          */

static bool        _pow = true;
static std::vector<WcNet> _results;
static bool        _scanReady = false;
static WcState     _st = WC_IDLE;
static String      _joinSsid, _joinPass;
static uint32_t    _joinT0 = 0, _scanT0 = 0, _failT0 = 0;
static bool        _joinOk = false, _joinFail = false;
static bool        _wantScan = false;

static String loadCreds() {
  Preferences p;
  String s;
  if (p.begin(PREF_WIFI, true)) { s = p.getString("creds", ""); p.end(); }
  return s;
}
static void saveCreds(const String& s) {
  Preferences p;
  if (p.begin(PREF_WIFI, false)) { p.putString("creds", s); p.end(); }
}

static bool isHotspotName(const String& s) {
  String t = s; t.toLowerCase();
  const char* keys[] = { "hotspot", "iphone", "pixel", "galaxy", "android",
                         "samsung", "mobile", "oneplus", "xiaomi", "honor",
                         "tether", "studio-hot" };
  for (auto k : keys) if (t.indexOf(k) >= 0) return true;
  return false;
}

void wcBegin() {
  Preferences p;
  if (p.begin(PREF_WIFI, true)) {
    _pow = p.getUChar("pow", 1) != 0;
    p.end();
  }
  WiFi.mode(_pow ? WIFI_STA : WIFI_OFF);
  /* Modem sleep wakes in DTIM windows -> latency spikes that starve the
   * stream buffer mid-frame (audible micro-stalls on 96 kbps stereo NPR).
   * The legacy wifi_manager already ran setSleep(false); wcBegin must too. */
  WiFi.setSleep(false);
  _st = _pow ? WC_IDLE : WC_OFF;
  if (_pow && wcHasAnyCreds()) {
    String s, pass;
    Preferences q;
    if (q.begin(PREF_WIFI, true)) {
      s = q.getString(PREF_SSID, "");
      pass = q.getString(PREF_PASS, "");
      q.end();
    }
    if (s.length()) {
      bool any = false;
      String creds = loadCreds();
      DynamicJsonDocument d(4096);
      if (!creds.isEmpty() && !deserializeJson(d, creds)) {
        if (d[s].is<const char*>()) { pass = d[s].as<const char*>(); any = true; }
      }
      (void)any;
      wcJoinSaved(s);   /* quiet: picks the 802.1X path for "@eap:" creds */
    }
  }
}

void wcSetPower(bool on) {
  if (_pow == on) return;
  _pow = on;
  Preferences p;
  if (p.begin(PREF_WIFI, false)) { p.putUChar("pow", on ? 1 : 0); p.end(); }
  if (!on) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    _st = WC_OFF;
    _scanReady = false; _wantScan = false;
    _results.clear();
  } else {
    WiFi.mode(WIFI_STA);
    _st = WC_IDLE;
    if (wcHasAnyCreds()) wcBegin();   // rejoin stored network
  }
  LOG_I("wifi", "power %s", on ? "on" : "off");
}
bool wcPowered() { return _pow; }

WcState wcState() { return _st; }
bool wcConnecting() { return _st == WC_CONNECTING; }
bool wcConnected() { return _st == WC_CONNECTED; }
String wcSsid() { return WiFi.SSID(); }
const char* wcStateText() {
  switch (_st) {
    case WC_OFF: return "Off";
    case WC_CONNECTED: return "Connected";
    case WC_CONNECTING: return "Connecting";
    case WC_FAILED: return "Failed";
    default: return "On · no network";
  }
}

/* ---- scan ------------------------------------------------------------ */
void wcScanAsync() {
  if (!_pow) return;
  /* A previous scan that never reported completion keeps the driver's SCANNING
   * bit set, and the guard below would then refuse every later request - which
   * is what "the rescan icon does nothing" looks like. Clear it first. */
  if (_wantScan && millis() - _scanT0 > 8000) {
    LOG_W("wifi", "previous scan never finished - clearing it");
    WiFi.scanDelete();
    _wantScan = false;
  }
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  /* Scan request while a join attempt runs: stop the attempt, then scan.
   * The old early return left the Wi-Fi page with an EMPTY list (and a
   * "Scanning" spinner that never resolved) whenever the board retried a
   * stored network - the normal state at a new place, and the reason the
   * owner's iPhone hotspot was never listed. WC_FAILED re-arms the
   * stored-network join after the scan (see wcLoop). */
  if (_st == WC_CONNECTING) {
    WiFi.disconnect();
    _st = WC_FAILED;
    _failT0 = millis();
    LOG_W("wifi", "scan cancels join attempt: %s", _joinSsid.c_str());
    /* Let the driver finish the teardown before the scan: a scan started
     * inside a connect attempt gets aborted part-way and then reports only
     * the strongest AP or nothing at all. */
    delay(250);
  }
  _scanReady = false;
  /* show_hidden = true: an iPhone personal hotspot can advertise a hidden
   * SSID until a device joins it, and a hidden-filtered scan never lists it. */
  int16_t rc = WiFi.scanNetworks(true /* async */, true /* hidden */,
                                 false /* passive */, 300 /* ms per channel */);
  LOG_I("wifi", "scan start rc=%d st=%d", (int)rc, (int)_st);
  _scanT0 = millis();
  _wantScan = true;
}

bool wcScanReady() {
  if (!_scanReady) return false;
  _scanReady = false;
  return true;
}

std::vector<WcNet>& wcResults() { return _results; }

static void collectScan() {
  int n = WiFi.scanComplete();
  if (n < 0) return;
  _results.clear();
  String cur = WiFi.SSID();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    int32_t r = WiFi.RSSI(i);
    bool dup = false;
    for (auto& x : _results) if (x.ssid == s) {
      dup = true;
      if (r > x.rssi) x.rssi = r;
      break;
    }
    if (dup) continue;
    WcNet w;
    w.ssid = s;
    w.rssi = r;
    w.lvl = r > -55 ? 3 : (r > -67 ? 2 : 1);
    wifi_auth_mode_t e = WiFi.encryptionType(i);
    w.open = (e == WIFI_AUTH_OPEN);
    w.eap = (e == WIFI_AUTH_WPA2_ENTERPRISE);   // 802.1X: user name + password
    w.five = (WiFi.channel(i) >= 14);
    w.hot = isHotspotName(s);
    w.saved = wcHasSaved(s) || s == cur;
    _results.push_back(w);
  }
  WiFi.scanDelete();
  std::sort(_results.begin(), _results.end(),
            [](const WcNet& a, const WcNet& b) { return a.rssi > b.rssi; });
  /* connected network first (prototype renderNets ordering) */
  for (size_t i = 0; i < _results.size(); i++) {
    if (_results[i].ssid == cur && _st == WC_CONNECTED && i > 0) {
      WcNet t = _results[i];
      _results.erase(_results.begin() + i);
      _results.insert(_results.begin(), t);
      break;
    }
  }
  _scanReady = true;
  _wantScan = false;
  LOG_I("wifi", "scan: %d nets", (int)_results.size());
  /* Per-network line with the RSSI: this is the only way to compare the
   * board's radio against a phone in the same spot (an empty or 1-entry
   * list cannot be told apart from a scan that was cut short). */
  for (size_t i = 0; i < _results.size() && i < 8; i++) {
    LOG_I("wifi", "  net%u %.20s rssi=%d %s", (unsigned)i,
          _results[i].ssid.c_str(), (int)_results[i].rssi,
          _results[i].five ? "5G" : "2.4G");
  }
}

/* ---- join ------------------------------------------------------------ */
void wcJoinAsync(const String& ssid, const String& pass) {
  if (!_pow) return;
  _joinSsid = ssid; _joinPass = pass;
  _joinT0 = millis();
  _st = WC_CONNECTING;
  _joinOk = _joinFail = false;
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : NULL);
  LOG_I("wifi", "joining %s", ssid.c_str());
}

/* ---- 802.1X (enterprise) -----------------------------------------------
 * Credential form: "@eap:USER:PASSWORD" in the same store as WPA2 keys. */
#define EAP_PREFIX "@eap:"

bool wcEnterpriseSupported() {
#ifdef CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
  return true;
#else
  return false;
#endif
}

/* Split "@eap:user:pass" — false for a plain WPA2 passphrase. */
static bool splitEapCreds(const String& stored, String& user, String& pass) {
  if (!stored.startsWith(EAP_PREFIX)) return false;
  String rest = stored.substring(strlen(EAP_PREFIX));
  int c = rest.indexOf(':');
  if (c <= 0) return false;
  user = rest.substring(0, c);
  pass = rest.substring(c + 1);
  return user.length() > 0 && pass.length() > 0;
}

void wcJoinEnterpriseAsync(const String& ssid, const String& user, const String& pass) {
  if (!_pow) return;
  _joinSsid = ssid;
  _joinPass = String(EAP_PREFIX) + user + ":" + pass;   // stored as typed
  _joinT0 = millis();
  _st = WC_CONNECTING;
  _joinOk = _joinFail = false;
  WiFi.disconnect();
#ifdef CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
  /* PEAP/MSCHAPv2: identity = user name, no certificate to pin. */
  WiFi.begin(ssid.c_str(), WPA2_AUTH_PEAP, user.c_str(), user.c_str(), pass.c_str());
  LOG_I("wifi", "joining %s (enterprise PEAP as %s)", ssid.c_str(), user.c_str());
#else
  LOG_W("wifi", "no 802.1X in this build; cannot join %s", ssid.c_str());
  _st = WC_FAILED;
  _joinFail = true;
#endif
}

void wcJoinSaved(const String& ssid) {
  String stored = wcPassFor(ssid), user, pass;
  if (splitEapCreds(stored, user, pass)) wcJoinEnterpriseAsync(ssid, user, pass);
  else wcJoinAsync(ssid, stored);
}

static void storeCred(const String& ssid, const String& pass) {
  String creds = loadCreds();
  DynamicJsonDocument d(4096);
  if (!creds.isEmpty()) deserializeJson(d, creds);
  d[ssid] = pass;
  String out;
  serializeJson(d, out);
  saveCreds(out);
  Preferences p;                       // keep legacy pair in sync
  if (p.begin(PREF_WIFI, false)) {
    p.putString(PREF_SSID, ssid);
    p.putString(PREF_PASS, pass);
    p.end();
  }
}

void wcDisconnect() {
  WiFi.disconnect();
  if (_st == WC_CONNECTED) _st = WC_IDLE;
}

void wcForget(const String& ssid) {
  String creds = loadCreds();
  DynamicJsonDocument d(4096);
  if (!creds.isEmpty() && !deserializeJson(d, creds)) {
    d.remove(ssid);
    String out; serializeJson(d, out);
    saveCreds(out);
  }
  Preferences p;
  if (p.begin(PREF_WIFI, false)) {
    if (p.getString(PREF_SSID, "") == ssid) {
      p.putString(PREF_SSID, "");
      p.putString(PREF_PASS, "");
    }
    p.end();
  }
  if (WiFi.SSID() == ssid) WiFi.disconnect();
}

std::vector<String> wcSavedList() {
  std::vector<String> out;
  String creds = loadCreds();
  DynamicJsonDocument d(4096);
  if (!creds.isEmpty() && !deserializeJson(d, creds)) {
    for (JsonPairConst kv : d.as<JsonObjectConst>())
      out.push_back(String(kv.key().c_str()));
  }
  Preferences p;
  if (p.begin(PREF_WIFI, true)) {
    String s = p.getString(PREF_SSID, "");
    p.end();
    if (s.length()) {
      bool have = false;
      for (auto& x : out) if (x == s) { have = true; break; }
      if (!have) out.push_back(s);
    }
  }
  return out;
}

bool wcHasSaved(const String& ssid) {
  if (!ssid.length()) return false;
  for (auto& s : wcSavedList()) if (s == ssid) return true;
  return false;
}
bool wcHasAnyCreds() { return !wcSavedList().empty(); }

String wcPassFor(const String& ssid) {
  String creds = loadCreds();
  DynamicJsonDocument d(4096);
  if (!creds.isEmpty() && !deserializeJson(d, creds) && d[ssid].is<const char*>())
    return String(d[ssid].as<const char*>());
  Preferences p;
  String s, pass;
  if (p.begin(PREF_WIFI, true)) {
    s = p.getString(PREF_SSID, ""); pass = p.getString(PREF_PASS, "");
    p.end();
  }
  return (s == ssid) ? pass : String();
}

bool wcJoinOk() { bool v = _joinOk; _joinOk = false; return v; }
bool wcJoinFailed() { bool v = _joinFail; _joinFail = false; return v; }
void wcClearJoins() { _joinOk = _joinFail = false; }

/* ---- loop ------------------------------------------------------------ */
void wcLoop() {
  if (!_pow) return;

  if (_wantScan) {
    static bool scanRetried = false;   /* one automatic retry per request */
    int16_t sc = WiFi.scanComplete();
    if (sc >= 0) {
      scanRetried = false;
      collectScan();
    } else if (sc == WIFI_SCAN_FAILED || millis() - _scanT0 > 15000) {
      /* A scan can be refused or dropped mid-way (a driver transition, or a
       * join attempt on the same radio). Retry ONCE, then tell the page to
       * stop spinning and record what happened. */
      WiFi.scanDelete();
      if (!scanRetried) {
        scanRetried = true;
        LOG_E("wifi", "scan did not complete rc=%d after %u ms - retrying",
              (int)sc, (unsigned)(millis() - _scanT0));
        _scanReady = false;
        _wantScan = true;
        _scanT0 = millis();
        WiFi.scanNetworks(true /* async */, true /* hidden */,
                          false /* passive */, 300 /* ms per channel */);
      } else {
        scanRetried = false;
        LOG_E("wifi", "scan failed twice rc=%d - showing what we have", (int)sc);
        _wantScan = false;
        _scanReady = true;
      }
    }
  }

  if (_st == WC_CONNECTING) {
    wl_status_t s = WiFi.status();
    if (s == WL_CONNECTED) {
      _st = WC_CONNECTED;
      _joinOk = true;
      storeCred(_joinSsid, _joinPass);
      LOG_I("wifi", "connected: %s (%s)", _joinSsid.c_str(),
            WiFi.localIP().toString().c_str());
    } else if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL ||
               millis() - _joinT0 > 15000) {
      _st = WC_FAILED;
      _failT0 = millis();
      _joinFail = true;
      LOG_E("wifi", "join failed: %s", _joinSsid.c_str());
    }
  } else if (_st == WC_CONNECTED) {
    if (WiFi.status() != WL_CONNECTED) {
      _st = WC_IDLE;
      LOG_W("wifi", "link lost");
      if (_joinSsid.length()) {          // come back by ourselves
        LOG_W("wifi", "rejoining %s", _joinSsid.c_str());
        wcJoinSaved(_joinSsid);
      }
    }
  } else if (_st == WC_FAILED) {
    /* Auto-retry a FAILED join for STORED networks every 5 s. A boot-time
     * join that fails (AP/DHCP hiccup) previously left Wi-Fi dead forever
     * (WC_FAILED is terminal) — wifi=1/0 + STREAM UNAVAILABLE until the user
     * toggled power. Never-saved nets (wrong password tap) keep the old
     * wait-for-user behaviour. Skip while a user scan is in flight. */
    bool scanning = _wantScan || WiFi.scanComplete() == WIFI_SCAN_RUNNING;
    if (!scanning && wcHasSaved(_joinSsid) && millis() - _failT0 >= 5000) {
      _failT0 = millis();
      LOG_W("wifi", "auto-retry join: %s", _joinSsid.c_str());
      wcJoinAsync(_joinSsid, wcPassFor(_joinSsid));
    }
  }
}
