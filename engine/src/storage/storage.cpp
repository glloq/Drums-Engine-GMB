#include "storage.h"

bool Storage::begin() {
  if (!LittleFS.begin(true)) {
    DBGLN("[Storage] LittleFS mount failed!");
    return false;
  }

  if (!LittleFS.exists(LOOPS_DIR)) {
    LittleFS.mkdir(LOOPS_DIR);
  }

  DBGF("[Storage] LittleFS OK - %s\n", formatInfo().c_str());
  return true;
}

bool Storage::fileExists(const char* path) {
  return LittleFS.exists(path);
}

// ============================================================================
// Actuators persistence
// ============================================================================

static void actuatorConfigToJson(const ActuatorConfig& cfg, JsonObject& obj) {
  obj["id"] = cfg.id;
  obj["name"] = cfg.name;
  obj["type"] = (uint8_t)cfg.type;
  obj["behavior"] = (uint8_t)cfg.behavior;
  obj["bus"] = (uint8_t)cfg.bus;
  obj["hwAddress"] = cfg.hwAddress;
  obj["hwPin"] = cfg.hwPin;
  obj["paramMin"] = cfg.paramMin;
  obj["paramMax"] = cfg.paramMax;
  obj["paramDefault"] = cfg.paramDefault;
  obj["cooldownUs"] = cfg.cooldownUs;
  obj["enabled"] = cfg.enabled;
  obj["inverted"] = cfg.inverted;
  if (cfg.endStopPin1 != 0xFF) obj["endStopPin1"] = cfg.endStopPin1;
  if (cfg.endStopPin2 != 0xFF) obj["endStopPin2"] = cfg.endStopPin2;
}

static void actuatorConfigFromJson(const JsonObject& obj, ActuatorConfig& cfg) {
  cfg.id = obj["id"] | 0;
  strlcpy(cfg.name, obj["name"] | "Actuator", sizeof(cfg.name));
  cfg.type = (ActuatorType)(obj["type"] | 0);
  cfg.behavior = (ActuatorBehavior)(obj["behavior"] | 0);
  cfg.bus = (HardwareBus)(obj["bus"] | 0);
  cfg.hwAddress = obj["hwAddress"] | 0x20;
  cfg.hwPin = obj["hwPin"] | 0;
  cfg.paramMin = obj["paramMin"] | 8;
  cfg.paramMax = obj["paramMax"] | 30;
  cfg.paramDefault = obj["paramDefault"] | 15;
  cfg.cooldownUs = obj["cooldownUs"] | 200;
  cfg.enabled = obj["enabled"] | true;
  cfg.inverted = obj["inverted"] | false;
  cfg.endStopPin1 = obj["endStopPin1"] | 0xFF;
  cfg.endStopPin2 = obj["endStopPin2"] | 0xFF;
}

bool Storage::saveActuators(const ActuatorFactory& factory) {
  JsonDocument doc;
  doc["version"] = 2;
  JsonArray arr = doc["actuators"].to<JsonArray>();

  for (uint8_t i = 0; i < factory.getConfigCount(); i++) {
    JsonObject obj = arr.add<JsonObject>();
    actuatorConfigToJson(factory.getConfig(i), obj);
  }

  return _writeJson(ACTUATORS_FILE, doc);
}

bool Storage::loadActuators(ActuatorFactory& factory) {
  JsonDocument doc;
  if (!_readJson(ACTUATORS_FILE, doc)) return false;

  JsonArray arr;
  if (doc.is<JsonObject>() && doc["version"].is<int>()) {
    // Version 2+: object wrapper with "actuators" array
    DBGF("[Storage] Actuators config version %d\n", doc["version"].as<int>());
    arr = doc["actuators"].as<JsonArray>();
  } else {
    // Legacy version 1: plain array at root
    DBGLN("[Storage] Actuators config legacy format (v1)");
    arr = doc.as<JsonArray>();
  }

  uint8_t loaded = 0;

  for (JsonObject obj : arr) {
    ActuatorConfig cfg;
    actuatorConfigFromJson(obj, cfg);
    if (factory.addConfig(cfg) >= 0) {
      loaded++;
    }
  }

  DBGF("[Storage] Loaded %d actuator configs\n", loaded);
  return loaded > 0;
}

// ============================================================================
// Migration V4: ancien format avec actionneurs inline dans instruments
// ============================================================================

bool Storage::loadLegacyV4(ActuatorFactory& factory, InstrumentManager& mgr) {
  JsonDocument doc;
  if (!_readJson(INSTRUMENTS_FILE, doc)) return false;

  JsonArray arr = doc.as<JsonArray>();
  uint8_t nextActId = 1;

  DBGLN("[Storage] Migrating from V4 legacy format...");

  for (JsonObject instObj : arr) {
    // Creer l'instrument
    InstrumentConfig inst;
    inst.id = instObj["id"] | 0;
    strlcpy(inst.name, instObj["name"] | "Unnamed", sizeof(inst.name));
    strlcpy(inst.category, instObj["category"] | "drums", sizeof(inst.category));
    inst.midiNote = instObj["midiNote"] | 36;
    inst.midiChannel = instObj["midiChannel"] | 10;
    inst.enabled = instObj["enabled"] | true;
    inst.actuatorCount = 0;

    // Migrer les actionneurs inline
    JsonArray actuators = instObj["actuators"].as<JsonArray>();
    if (!actuators.isNull()) {
      for (JsonObject actObj : actuators) {
        if (inst.actuatorCount >= MAX_ACTUATORS_PER_INST) break;

        ActuatorConfig actCfg;
        actCfg.id = nextActId++;
        strlcpy(actCfg.name, actObj["name"] | "Actuator", sizeof(actCfg.name));

        // Convertir l'ancien type en nouveau type + behavior
        uint8_t oldType = actObj["type"] | 0;
        switch (oldType) {
          case 0: // ACT_SOLENOID_STRIKE
            actCfg.type = ActuatorType::SOLENOID;
            actCfg.behavior = ActuatorBehavior::SOLENOID_STRIKE;
            break;
          case 1: // ACT_SOLENOID_HOLD
            actCfg.type = ActuatorType::SOLENOID;
            actCfg.behavior = ActuatorBehavior::SOLENOID_HOLD;
            break;
          case 2: // ACT_SERVO_POSITION
            actCfg.type = ActuatorType::SERVO;
            actCfg.behavior = ActuatorBehavior::SERVO_POSITION;
            break;
          case 3: // ACT_SERVO_STRIKE
            actCfg.type = ActuatorType::SERVO;
            actCfg.behavior = ActuatorBehavior::SERVO_STRIKE;
            break;
          case 4: // ACT_COMB_BRUSH
            actCfg.type = ActuatorType::SERVO;
            actCfg.behavior = ActuatorBehavior::COMB_BRUSH;
            break;
          case 5: // ACT_PITCH_BEND
            actCfg.type = ActuatorType::SERVO;
            actCfg.behavior = ActuatorBehavior::PITCH_BEND;
            break;
          default:
            actCfg.type = ActuatorType::SOLENOID;
            actCfg.behavior = ActuatorBehavior::SOLENOID_STRIKE;
            break;
        }

        actCfg.bus = (HardwareBus)(actObj["bus"] | 0);
        actCfg.hwAddress = actObj["hwAddress"] | 0x20;
        actCfg.hwPin = actObj["hwPin"] | 0;
        actCfg.paramMin = actObj["paramMin"] | 8;
        actCfg.paramMax = actObj["paramMax"] | 30;
        actCfg.paramDefault = actObj["paramDefault"] | 15;
        actCfg.cooldownUs = (uint16_t)(actObj["delayMs"] | 0) * 10; // Approx conversion
        actCfg.enabled = actObj["enabled"] | true;

        // Ajouter la config au factory
        factory.addConfig(actCfg);

        // Lier l'instrument a cet actionneur
        inst.actuatorIds[inst.actuatorCount] = actCfg.id;
        inst.actuatorCount++;
      }
    }

    mgr.addInstrument(inst);
  }

  DBGF("[Storage] Migrated %d instruments, %d actuators from V4 format\n",
       mgr.getInstrumentCount(), factory.getConfigCount());

  // Sauvegarder dans le nouveau format
  saveActuators(factory);
  saveInstruments(mgr);

  DBGLN("[Storage] V4 migration saved to new format");
  return true;
}

// ============================================================================
// Instruments
// ============================================================================

bool Storage::saveInstruments(const InstrumentManager& mgr) {
  JsonDocument doc;
  doc["version"] = 2;
  JsonArray arr = doc["instruments"].to<JsonArray>();
  mgr.toJson(arr);
  return _writeJson(INSTRUMENTS_FILE, doc);
}

bool Storage::loadInstruments(InstrumentManager& mgr) {
  JsonDocument doc;
  if (!_readJson(INSTRUMENTS_FILE, doc)) return false;

  JsonArray arr;
  if (doc.is<JsonObject>() && doc["version"].is<int>()) {
    // Version 2+: object wrapper with "instruments" array
    DBGF("[Storage] Instruments config version %d\n", doc["version"].as<int>());
    arr = doc["instruments"].as<JsonArray>();
  } else {
    // Legacy version 1: plain array at root
    DBGLN("[Storage] Instruments config legacy format (v1)");
    arr = doc.as<JsonArray>();
  }

  return mgr.fromJson(arr);
}

// ============================================================================
// System Config
// ============================================================================

bool Storage::saveSystemConfig(const JsonObject& cfg) {
  JsonDocument doc;
  doc.set(cfg);
  return _writeJson(CONFIG_FILE, doc);
}

bool Storage::loadSystemConfig(JsonObject& cfg) {
  JsonDocument doc;
  if (!_readJson(CONFIG_FILE, doc)) return false;
  cfg = doc.as<JsonObject>();
  return true;
}

// ============================================================================
// Loops
// ============================================================================

bool Storage::saveLoop(uint8_t id, const JsonObject& loop) {
  char path[32];
  snprintf(path, sizeof(path), "%s/loop_%d.json", LOOPS_DIR, id);
  JsonDocument doc;
  doc.set(loop);
  return _writeJson(path, doc);
}

bool Storage::loadLoop(uint8_t id, JsonObject& loop) {
  char path[32];
  snprintf(path, sizeof(path), "%s/loop_%d.json", LOOPS_DIR, id);
  JsonDocument doc;
  if (!_readJson(path, doc)) return false;
  loop = doc.as<JsonObject>();
  return true;
}

bool Storage::deleteLoop(uint8_t id) {
  char path[32];
  snprintf(path, sizeof(path), "%s/loop_%d.json", LOOPS_DIR, id);
  return LittleFS.remove(path);
}

bool Storage::listLoops(JsonArray& arr) {
  File dir = LittleFS.open(LOOPS_DIR);
  if (!dir || !dir.isDirectory()) return false;

  File file = dir.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String name = file.name();
      if (name.startsWith("loop_") && name.endsWith(".json")) {
        int id = name.substring(5, name.length() - 5).toInt();
        JsonDocument doc;
        if (_readJson(file.path(), doc)) {
          JsonObject obj = arr.add<JsonObject>();
          obj["id"] = id;
          obj["name"] = doc["name"] | "Unnamed Loop";
          obj["bpm"] = doc["bpm"] | MIDI_BPM_DEFAULT;
          obj["bars"] = doc["bars"] | 1;
          obj["eventCount"] = doc["events"].as<JsonArray>().size();
        }
      }
    }
    file = dir.openNextFile();
  }
  return true;
}

// ============================================================================
// Utility
// ============================================================================

size_t Storage::totalBytes() { return LittleFS.totalBytes(); }
size_t Storage::usedBytes() { return LittleFS.usedBytes(); }

String Storage::formatInfo() {
  char buf[64];
  snprintf(buf, sizeof(buf), "Used: %lu / %lu bytes (%.1f%%)",
           (unsigned long)usedBytes(), (unsigned long)totalBytes(),
           (float)usedBytes() / totalBytes() * 100.0f);
  return String(buf);
}

bool Storage::_writeJson(const char* path, const JsonDocument& doc) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    DBGF("[Storage] Failed to open %s for writing\n", path);
    return false;
  }
  size_t written = serializeJson(doc, file);
  file.close();
  DBGF("[Storage] Wrote %d bytes to %s\n", written, path);
  return written > 0;
}

bool Storage::_readJson(const char* path, JsonDocument& doc) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    DBGF("[Storage] File not found: %s\n", path);
    return false;
  }
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    DBGF("[Storage] JSON parse error in %s: %s\n", path, err.c_str());
    return false;
  }
  return true;
}
