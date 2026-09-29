#pragma once
/* Station manager (Prompt 4): station list persisted in LittleFS as
 * /stations.json, current station index in NVS. The web server and the
 * touch UI both read/write through this single source of truth, so
 * changes made in the browser show up on the device immediately.
 *
 * JSON format:
 *   { "stations": [ {"name": "...", "url": "...", "genre": "..."}, ... ],
 *     "current": 0 }
 */

#include <Arduino.h>

struct RadioStation {
  String name;
  String url;
  String genre;
};

class StationManager {
public:
  /* Mount LittleFS, load stations (seeding defaults if missing) and
   * restore the last-played index from NVS. Returns true if usable. */
  bool begin();

  /* --- read side (used by the UI) --- */
  uint8_t getStationCount() const { return _count; }
  RadioStation getStation(uint8_t index) const;
  RadioStation getCurrentStation() const { return getStation(_current); }
  uint8_t getCurrentIndex() const { return _current; }
  void setStation(uint8_t index);          // + persist to NVS
  void nextStation();
  void previousStation();

  /* --- write side (used by the web server) --- */
  bool addStation(const char* name, const char* url, const char* genre);
  bool updateStation(uint8_t index, const char* name, const char* url, const char* genre);
  bool deleteStation(uint8_t index);
  bool save();                             // write JSON to LittleFS

  static const uint8_t MAX_STATIONS = 32;

private:
  void seedDefaults();
  bool load();
  void persistCurrent();

  RadioStation _stations[MAX_STATIONS];
  uint8_t _count = 0;
  uint8_t _current = 0;
  bool _ok = false;
};

extern StationManager stationManager;   // global instance
