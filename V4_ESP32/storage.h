#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "instrumentManager.h"

// ============================================================================
// LittleFS persistence for instruments, loops, and system config
// ============================================================================

class Storage {
public:
  bool begin();

  // Instruments
  bool saveInstruments(const InstrumentManager& mgr);
  bool loadInstruments(InstrumentManager& mgr);

  // System config (WiFi, MIDI channel, etc.)
  bool saveSystemConfig(const JsonObject& cfg);
  bool loadSystemConfig(JsonObject& cfg);

  // Loops
  bool saveLoop(uint8_t id, const JsonObject& loop);
  bool loadLoop(uint8_t id, JsonObject& loop);
  bool deleteLoop(uint8_t id);
  bool listLoops(JsonArray& arr);

  // Utility
  size_t totalBytes();
  size_t usedBytes();
  String formatInfo();

private:
  bool _writeJson(const char* path, const JsonDocument& doc);
  bool _readJson(const char* path, JsonDocument& doc);
};

#endif // STORAGE_H
