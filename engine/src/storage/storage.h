#ifndef ENGINE_STORAGE_H
#define ENGINE_STORAGE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "../core/config.h"
#include "../core/types.h"
#include "../instrument/instrument_manager.h"
#include "../actuator/actuator_factory.h"

// Forward declarations
class LedEngine;

// ============================================================================
// Storage - Persistance LittleFS
// ============================================================================
// Sauvegarde et charge les configurations :
//   - Actionneurs (actuators.json)
//   - Instruments (instruments.json)
//   - Boucles (loops/)
//   - Config systeme (config.json)
//
// Supporte la migration depuis le format V4_ESP32 (ancien format inline).
// ============================================================================

class Storage {
public:
  bool begin();

  // Actuators
  bool saveActuators(const ActuatorFactory& factory);
  bool loadActuators(ActuatorFactory& factory);

  // Instruments
  bool saveInstruments(const InstrumentManager& mgr);
  bool loadInstruments(InstrumentManager& mgr);

  // Migration: charger ancien format V4 (actuators inclus dans instruments)
  bool loadLegacyV4(ActuatorFactory& factory, InstrumentManager& mgr);

  // System config
  bool saveSystemConfig(const JsonObject& cfg);
  bool loadSystemConfig(JsonObject& cfg);

  // Loops
  bool saveLoop(uint8_t id, const JsonObject& loop);
  bool loadLoop(uint8_t id, JsonObject& loop);
  bool deleteLoop(uint8_t id);
  bool listLoops(JsonArray& arr);

  // LED configuration
  bool saveLedConfig(const LedEngine& engine);
  bool loadLedConfig(LedEngine& engine);

  // LED themes
  bool saveLedThemes(const LedEngine& engine);
  bool loadLedThemes(LedEngine& engine);

  // Utility
  size_t totalBytes();
  size_t usedBytes();
  String formatInfo();

  // Generic JSON file (for small configs like MIDI settings)
  bool saveJsonFile(const char* path, const JsonDocument& doc);
  bool loadJsonFile(const char* path, JsonDocument& doc);

  // Verifier si un fichier existe
  bool fileExists(const char* path);

private:
  bool _writeJson(const char* path, const JsonDocument& doc);
  bool _readJson(const char* path, JsonDocument& doc);
};

#endif // ENGINE_STORAGE_H
