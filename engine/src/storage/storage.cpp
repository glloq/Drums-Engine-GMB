#include "storage.h"
#include "../led/led_engine.h"

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

bool Storage::saveJsonFile(const char* path, const JsonDocument& doc) {
  return _writeJson(path, doc);
}

bool Storage::loadJsonFile(const char* path, JsonDocument& doc) {
  return _readJson(path, doc);
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

// M3 fix: Caller owns the JsonDocument, so JsonObject references remain valid
bool Storage::loadSystemConfig(JsonDocument& doc) {
  return _readJson(CONFIG_FILE, doc);
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

// M3 fix: Caller owns the JsonDocument, so JsonObject references remain valid
bool Storage::loadLoop(uint8_t id, JsonDocument& doc) {
  char path[32];
  snprintf(path, sizeof(path), "%s/loop_%d.json", LOOPS_DIR, id);
  return _readJson(path, doc);
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
  // Atomic write: write to .tmp then rename to avoid corruption on crash
  String tmpPath = String(path) + ".tmp";
  File file = LittleFS.open(tmpPath.c_str(), "w");
  if (!file) {
    DBGF("[Storage] Failed to open %s for writing\n", tmpPath.c_str());
    return false;
  }
  size_t written = serializeJson(doc, file);
  file.close();

  if (written == 0) {
    LittleFS.remove(tmpPath.c_str());
    DBGF("[Storage] Write failed (0 bytes) for %s\n", path);
    return false;
  }

  // M4 fix: LittleFS.rename overwrites dest atomically — no need to remove first
  // Removing first creates a window where both files are gone if power fails
  LittleFS.rename(tmpPath.c_str(), path);
  DBGF("[Storage] Wrote %d bytes to %s\n", written, path);
  return true;
}

// ============================================================================
// LED Configuration persistence
// ============================================================================

static void rgbToJsonArr(const RgbColor& c, JsonObject& obj) {
  obj["r"] = c.r; obj["g"] = c.g; obj["b"] = c.b;
}

static RgbColor rgbFromJsonObj(const JsonObject& obj) {
  return RgbColor(obj["r"] | 0, obj["g"] | 0, obj["b"] | 0);
}

bool Storage::saveLedConfig(const LedEngine& engine) {
  JsonDocument doc;
  doc["version"] = 1;

  // Strips
  JsonArray strips = doc["strips"].to<JsonArray>();
  for (uint8_t i = 0; i < LED_MAX_STRIPS; i++) {
    const LedStripConfig& cfg = engine.getStripConfig(i);
    if (cfg.ledCount == 0 && !cfg.enabled) continue;
    JsonObject obj = strips.add<JsonObject>();
    obj["id"] = cfg.id;
    obj["gpioPin"] = cfg.gpioPin;
    obj["ledCount"] = cfg.ledCount;
    obj["chipType"] = cfg.chipType;
    obj["colorOrder"] = cfg.colorOrder;
    obj["maxBrightness"] = cfg.maxBrightness;
    obj["enabled"] = cfg.enabled;
  }

  // Segments
  JsonArray segments = doc["segments"].to<JsonArray>();
  for (uint8_t i = 0; i < engine.getSegmentCount(); i++) {
    // Access segments through const-correct pattern
    const LedEngine& ce = engine;
    // We need non-const access for getSegmentByIndex, but we only read here
    LedEngine& ncEngine = const_cast<LedEngine&>(engine);
    LedSegmentConfig* seg = ncEngine.getSegmentByIndex(i);
    if (!seg) continue;

    JsonObject obj = segments.add<JsonObject>();
    obj["id"] = seg->id;
    obj["name"] = seg->name;
    obj["stripId"] = seg->stripId;
    obj["pixelStart"] = seg->pixelStart;
    obj["pixelCount"] = seg->pixelCount;
    obj["instrumentId"] = seg->instrumentId;

    JsonObject hitColor = obj["hitColor"].to<JsonObject>();
    rgbToJsonArr(seg->hitColor, hitColor);
    obj["hitBrightness"] = seg->hitBrightness;
    obj["hitFadeDurationMs"] = seg->hitFadeDurationMs;
    obj["hitAnimation"] = seg->hitAnimation;

    JsonObject idleColor = obj["idleColor"].to<JsonObject>();
    rgbToJsonArr(seg->idleColor, idleColor);
    obj["idleBrightness"] = seg->idleBrightness;
    obj["idleAnimation"] = seg->idleAnimation;

    obj["velocityToBrightness"] = seg->velocityToBrightness;
    obj["enabled"] = seg->enabled;
  }

  doc["masterBrightness"] = engine.getMasterBrightness();

  return _writeJson(LEDS_FILE, doc);
}

bool Storage::loadLedConfig(LedEngine& engine) {
  JsonDocument doc;
  if (!_readJson(LEDS_FILE, doc)) return false;

  // Strips
  JsonArray strips = doc["strips"].as<JsonArray>();
  if (!strips.isNull()) {
    for (JsonObject obj : strips) {
      LedStripConfig cfg;
      cfg.id = obj["id"] | 0;
      cfg.gpioPin = obj["gpioPin"] | LED_DEFAULT_PIN_0;
      cfg.ledCount = obj["ledCount"] | 0;
      cfg.chipType = obj["chipType"] | 0;
      cfg.colorOrder = obj["colorOrder"] | 0;
      cfg.maxBrightness = obj["maxBrightness"] | 128;
      cfg.enabled = obj["enabled"] | true;

      if (cfg.id < LED_MAX_STRIPS) {
        engine.configureStrip(cfg.id, cfg);
      }
    }
  }

  // Segments
  JsonArray segments = doc["segments"].as<JsonArray>();
  if (!segments.isNull()) {
    for (JsonObject obj : segments) {
      LedSegmentConfig seg;
      seg.id = obj["id"] | 0;
      strlcpy(seg.name, obj["name"] | "Segment", sizeof(seg.name));
      seg.stripId = obj["stripId"] | 0;
      seg.pixelStart = obj["pixelStart"] | 0;
      seg.pixelCount = obj["pixelCount"] | 1;
      seg.instrumentId = obj["instrumentId"] | 0xFF;

      if (obj["hitColor"].is<JsonObject>()) {
        seg.hitColor = rgbFromJsonObj(obj["hitColor"]);
      }
      seg.hitBrightness = obj["hitBrightness"] | 255;
      seg.hitFadeDurationMs = obj["hitFadeDurationMs"] | 300;
      seg.hitAnimation = obj["hitAnimation"] | 0;

      if (obj["idleColor"].is<JsonObject>()) {
        seg.idleColor = rgbFromJsonObj(obj["idleColor"]);
      }
      seg.idleBrightness = obj["idleBrightness"] | 30;
      seg.idleAnimation = obj["idleAnimation"] | 0;

      seg.velocityToBrightness = obj["velocityToBrightness"] | true;
      seg.enabled = obj["enabled"] | true;

      engine.addSegment(seg);
    }
  }

  uint8_t masterBri = doc["masterBrightness"] | 128;
  engine.setMasterBrightness(masterBri);

  DBGF("[Storage] Loaded LED config: %d segments\n", engine.getSegmentCount());
  return true;
}

bool Storage::saveLedThemes(const LedEngine& engine) {
  JsonDocument doc;
  doc["version"] = 1;
  doc["activeThemeId"] = engine.getActiveThemeId();

  JsonArray themes = doc["themes"].to<JsonArray>();
  LedEngine& ncEngine = const_cast<LedEngine&>(engine);
  for (uint8_t i = 0; i < engine.getThemeCount(); i++) {
    LedThemeConfig* theme = ncEngine.getThemeByIndex(i);
    if (!theme) continue;

    JsonObject obj = themes.add<JsonObject>();
    obj["id"] = theme->id;
    obj["name"] = theme->name;
    obj["globalBrightness"] = theme->globalBrightness;
    obj["idleMode"] = theme->idleMode;
    obj["hitMode"] = theme->hitMode;
    obj["fadeSpeed"] = theme->fadeSpeed;
    obj["enabled"] = theme->enabled;

    JsonArray palette = obj["palette"].to<JsonArray>();
    for (uint8_t j = 0; j < LED_THEME_PALETTE_SIZE; j++) {
      JsonObject c = palette.add<JsonObject>();
      rgbToJsonArr(theme->palette[j], c);
    }
  }

  return _writeJson(THEMES_FILE, doc);
}

bool Storage::loadLedThemes(LedEngine& engine) {
  JsonDocument doc;
  if (!_readJson(THEMES_FILE, doc)) return false;

  JsonArray themes = doc["themes"].as<JsonArray>();
  if (!themes.isNull()) {
    for (JsonObject obj : themes) {
      LedThemeConfig theme;
      theme.id = obj["id"] | 0;
      strlcpy(theme.name, obj["name"] | "Theme", sizeof(theme.name));
      theme.globalBrightness = obj["globalBrightness"] | 128;
      theme.idleMode = obj["idleMode"] | 0;
      theme.hitMode = obj["hitMode"] | 0;
      theme.fadeSpeed = obj["fadeSpeed"] | 5;
      theme.enabled = obj["enabled"] | true;

      JsonArray palette = obj["palette"].as<JsonArray>();
      if (!palette.isNull()) {
        uint8_t j = 0;
        for (JsonObject c : palette) {
          if (j >= LED_THEME_PALETTE_SIZE) break;
          theme.palette[j] = rgbFromJsonObj(c);
          j++;
        }
      }

      engine.addTheme(theme);
    }
  }

  uint8_t activeId = doc["activeThemeId"] | 0xFF;
  if (activeId != 0xFF) engine.setActiveTheme(activeId);

  DBGF("[Storage] Loaded %d LED themes\n", engine.getThemeCount());
  return true;
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
