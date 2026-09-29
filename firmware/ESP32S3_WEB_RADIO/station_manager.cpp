#include "station_manager.h"
#include "config.h"
#include "debug.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#define STATIONS_FILE "/stations.json"
#define PREF_RADIO "radio"
#define PREF_CUR   "cur"

StationManager stationManager;   // global instance

// ------------------------------------------------------------------
bool StationManager::begin() {
  if (!LittleFS.begin(true)) {           // true = format on mount failure
    LOG_E("stations", "LittleFS mount failed");
    return _ok = false;
  }
  LOG_I("stations", "LittleFS mounted (%llu bytes free)",
        (unsigned long long)LittleFS.totalBytes() - LittleFS.usedBytes());

  if (!load()) {
    LOG_W("stations", "no stations file — seeding defaults");
    seedDefaults();
    save();
  }

  // restore last-played index
  Preferences prefs;
  if (prefs.begin(PREF_RADIO, true)) {
    _current = prefs.getUChar(PREF_CUR, 0);
    prefs.end();
  }
  if (_current >= _count) _current = 0;

  LOG_I("stations", "%u stations loaded, current = %u", _count, _current);
  return _ok = true;
}

RadioStation StationManager::getStation(uint8_t index) const {
  if (index < _count) return _stations[index];
  RadioStation empty;
  return empty;
}

void StationManager::setStation(uint8_t index) {
  if (index < _count) {
    _current = index;
    persistCurrent();
  }
}

void StationManager::nextStation() {
  if (_count == 0) return;
  setStation((_current + 1) % _count);
}

void StationManager::previousStation() {
  if (_count == 0) return;
  setStation(_current == 0 ? _count - 1 : _current - 1);
}

// ------------------------------------------------------------------
bool StationManager::load() {
  if (!LittleFS.exists(STATIONS_FILE)) return false;

  File f = LittleFS.open(STATIONS_FILE, "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    LOG_E("stations", "JSON parse error: %s", err.c_str());
    return false;
  }

  JsonArray arr = doc["stations"].as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) return false;

  _count = 0;
  for (JsonObject o : arr) {
    if (_count >= MAX_STATIONS) break;
    _stations[_count].name  = o["name"] | "";
    _stations[_count].url   = o["url"] | "";
    _stations[_count].genre = o["genre"] | "";
    if (_stations[_count].name.length() == 0 ||
        _stations[_count].url.length() == 0) {
      continue;                            // skip invalid entries
    }
    _count++;
  }
  return _count > 0;
}

bool StationManager::save() {
  File f = LittleFS.open(STATIONS_FILE, "w");
  if (!f) {
    LOG_E("stations", "cannot open %s for writing", STATIONS_FILE);
    return false;
  }

  JsonDocument doc;
  JsonArray arr = doc["stations"].to<JsonArray>();
  for (uint8_t i = 0; i < _count; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["name"]  = _stations[i].name;
    o["url"]   = _stations[i].url;
    o["genre"] = _stations[i].genre;
  }
  doc["current"] = _current;

  bool ok = serializeJson(doc, f) > 0;
  f.close();
  if (!ok) {
    LOG_E("stations", "write failed");
    return false;
  }
  LOG_I("stations", "saved %u stations to %s", _count, STATIONS_FILE);
  return true;
}

bool StationManager::addStation(const char* name, const char* url, const char* genre) {
  if (_count >= MAX_STATIONS) return false;
  if (!name || !*name || !url || !*url) return false;

  _stations[_count].name  = name;
  _stations[_count].url   = url;
  _stations[_count].genre = genre ? genre : "";
  _count++;
  return save();
}

bool StationManager::updateStation(uint8_t index, const char* name,
                                   const char* url, const char* genre) {
  if (index >= _count) return false;
  if (!name || !*name || !url || !*url) return false;
  _stations[index].name  = name;
  _stations[index].url   = url;
  _stations[index].genre = genre ? genre : "";
  return save();
}

bool StationManager::deleteStation(uint8_t index) {
  if (index >= _count) return false;
  for (uint8_t i = index; i + 1 < _count; i++) {
    _stations[i] = _stations[i + 1];
  }
  _count--;
  if (_current >= _count && _count > 0) _current = _count - 1;
  return save();
}

// ------------------------------------------------------------------
void StationManager::seedDefaults() {
  /* Signal UI station set — identical order/data to the prototype. */
  static const struct { const char* n; const char* u; const char* g; } def[] = {
    {"Bloomberg Radio",
     "https://playerservices.streamtheworld.com/api/livestream-redirect/WBBRAMAAC.aac",
     "News \xc2\xb7 Business"},
    {"NPR News and Culture",
     "https://npr-ice.streamguys1.com/live.mp3",
     "News \xc2\xb7 Public radio"},
    {"BBC World Service",
     "https://vprbbc.streamguys1.com/vprbbc24.mp3",
     "News \xc2\xb7 World"},
    {"WNYC 93.9 FM",
     "https://fm939.wnyc.org/wnycfm",
     "Public radio \xc2\xb7 New York"},
    {"KQED Public Radio",
     "https://streams.kqed.org/kqedradio?onsite=true",
     "Public radio \xc2\xb7 Bay Area"},
    {"CBC Radio One",
     "https://26183.live.streamtheworld.com/CBLAFM_CBC_SC",
     "Public radio \xc2\xb7 Canada"},
    {"SomaFM \xc2\xb7 Groove Salad",
     "http://ice1.somafm.com/groovesalad-128-mp3",
     "Music \xc2\xb7 Ambient"},
    {"Radio Paradise \xc2\xb7 Main Mix",
     "http://stream.radioparadise.com/mp3-128",
     "Music \xc2\xb7 Eclectic"},
    {"Jazz24",
     "https://knkx-live-a.edge.audiocdn.com/6285_128k",
     "Music \xc2\xb7 Jazz"},
  };
  _count = sizeof(def) / sizeof(def[0]);
  for (uint8_t i = 0; i < _count; i++) {
    _stations[i].name  = def[i].n;
    _stations[i].url   = def[i].u;
    _stations[i].genre = def[i].g;
  }
  _current = 0;
}

void StationManager::persistCurrent() {
  Preferences prefs;
  if (prefs.begin(PREF_RADIO, false)) {
    prefs.putUChar(PREF_CUR, _current);
    prefs.end();
  }
}
