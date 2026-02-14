#ifndef ENGINE_STORAGE_H
#define ENGINE_STORAGE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "../core/config.h"
#include "../instrument/instrument_manager.h"

// ============================================================================
// Storage - Persistance LittleFS
// ============================================================================

class Storage {
public:
  bool begin();

  // Instruments
  bool saveInstruments(const InstrumentManager& mgr);
  bool loadInstruments(InstrumentManager& mgr);

  // System config
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

#endif // ENGINE_STORAGE_H
