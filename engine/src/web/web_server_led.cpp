#include "web_server.h"

// ============================================================================
// LED API Handlers
// ============================================================================

// --- Helper: serialize RgbColor ---
static void rgbToJson(const RgbColor& c, JsonObject& obj) {
  obj["r"] = c.r;
  obj["g"] = c.g;
  obj["b"] = c.b;
}

static RgbColor rgbFromJson(const JsonObject& obj) {
  return RgbColor(obj["r"] | 0, obj["g"] | 0, obj["b"] | 0);
}

// --- Helper: serialize strip config ---
static void stripConfigToJson(const LedStripConfig& cfg, JsonObject& obj) {
  obj["id"] = cfg.id;
  obj["gpioPin"] = cfg.gpioPin;
  obj["ledCount"] = cfg.ledCount;
  obj["chipType"] = cfg.chipType;
  obj["colorOrder"] = cfg.colorOrder;
  obj["maxBrightness"] = cfg.maxBrightness;
  obj["enabled"] = cfg.enabled;
}

// --- Helper: serialize segment config ---
static void segmentConfigToJson(const LedSegmentConfig& seg, JsonObject& obj) {
  obj["id"] = seg.id;
  obj["name"] = seg.name;
  obj["stripId"] = seg.stripId;
  obj["pixelStart"] = seg.pixelStart;
  obj["pixelCount"] = seg.pixelCount;
  obj["instrumentId"] = seg.instrumentId;

  JsonObject hitColorObj = obj["hitColor"].to<JsonObject>();
  rgbToJson(seg.hitColor, hitColorObj);
  obj["hitBrightness"] = seg.hitBrightness;
  obj["hitFadeDurationMs"] = seg.hitFadeDurationMs;
  obj["hitAnimation"] = seg.hitAnimation;

  JsonObject idleColorObj = obj["idleColor"].to<JsonObject>();
  rgbToJson(seg.idleColor, idleColorObj);
  obj["idleBrightness"] = seg.idleBrightness;
  obj["idleAnimation"] = seg.idleAnimation;

  obj["velocityToBrightness"] = seg.velocityToBrightness;
  obj["enabled"] = seg.enabled;
}

static void segmentConfigFromJson(const JsonObject& obj, LedSegmentConfig& seg) {
  seg.id = obj["id"] | 0;
  strlcpy(seg.name, obj["name"] | "Segment", sizeof(seg.name));
  seg.stripId = obj["stripId"] | 0;
  seg.pixelStart = obj["pixelStart"] | 0;
  seg.pixelCount = obj["pixelCount"] | 1;
  seg.instrumentId = obj["instrumentId"] | 0xFF;

  if (obj["hitColor"].is<JsonObject>()) {
    seg.hitColor = rgbFromJson(obj["hitColor"]);
  }
  seg.hitBrightness = obj["hitBrightness"] | 255;
  seg.hitFadeDurationMs = obj["hitFadeDurationMs"] | 300;
  seg.hitAnimation = obj["hitAnimation"] | 0;

  if (obj["idleColor"].is<JsonObject>()) {
    seg.idleColor = rgbFromJson(obj["idleColor"]);
  }
  seg.idleBrightness = obj["idleBrightness"] | 30;
  seg.idleAnimation = obj["idleAnimation"] | 0;

  seg.velocityToBrightness = obj["velocityToBrightness"] | true;
  seg.enabled = obj["enabled"] | true;
}

// --- Helper: serialize theme config ---
static void themeConfigToJson(const LedThemeConfig& theme, JsonObject& obj) {
  obj["id"] = theme.id;
  obj["name"] = theme.name;
  obj["globalBrightness"] = theme.globalBrightness;
  obj["idleMode"] = theme.idleMode;
  obj["hitMode"] = theme.hitMode;
  obj["fadeSpeed"] = theme.fadeSpeed;
  obj["enabled"] = theme.enabled;

  JsonArray paletteArr = obj["palette"].to<JsonArray>();
  for (uint8_t i = 0; i < LED_THEME_PALETTE_SIZE; i++) {
    JsonObject c = paletteArr.add<JsonObject>();
    rgbToJson(theme.palette[i], c);
  }
}

static void themeConfigFromJson(const JsonObject& obj, LedThemeConfig& theme) {
  theme.id = obj["id"] | 0;
  strlcpy(theme.name, obj["name"] | "Theme", sizeof(theme.name));
  theme.globalBrightness = obj["globalBrightness"] | 128;
  theme.idleMode = obj["idleMode"] | 0;
  theme.hitMode = obj["hitMode"] | 0;
  theme.fadeSpeed = obj["fadeSpeed"] | 5;
  theme.enabled = obj["enabled"] | true;

  JsonArray paletteArr = obj["palette"].as<JsonArray>();
  if (!paletteArr.isNull()) {
    uint8_t i = 0;
    for (JsonObject c : paletteArr) {
      if (i >= LED_THEME_PALETTE_SIZE) break;
      theme.palette[i] = rgbFromJson(c);
      i++;
    }
  }
}

// ============================================================================
// Strips
// ============================================================================

void WebServerManager::_handleGetLedStrips(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray arr = doc["strips"].to<JsonArray>();

  for (uint8_t i = 0; i < LED_MAX_STRIPS; i++) {
    const LedStripConfig& cfg = _ledEngine->getStripConfig(i);
    if (cfg.ledCount > 0 || cfg.enabled) {
      JsonObject obj = arr.add<JsonObject>();
      stripConfigToJson(cfg, obj);
      obj["active"] = _ledEngine->getActiveStripCount() > 0;
    }
  }

  _sendJson(req, 200, doc);
}

void WebServerManager::_handleConfigureLedStrip(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (!_parseJsonBody(req, data, len, doc)) return;

  uint8_t stripIdx = doc["id"] | 0xFF;
  if (stripIdx >= LED_MAX_STRIPS) {
    _sendError(req, 400, "Strip id must be 0-3");
    return;
  }

  LedStripConfig cfg;
  cfg.id = stripIdx;
  cfg.gpioPin = doc["gpioPin"] | LED_DEFAULT_PIN_0;
  cfg.ledCount = doc["ledCount"] | 0;
  cfg.chipType = doc["chipType"] | 0;
  cfg.colorOrder = doc["colorOrder"] | 0;
  cfg.maxBrightness = doc["maxBrightness"] | 128;
  cfg.enabled = doc["enabled"] | true;

  if (cfg.ledCount > LED_MAX_PIXELS) {
    _sendError(req, 400, "Led count exceeds max");
    return;
  }

  if (!_ledEngine->configureStrip(stripIdx, cfg)) {
    _sendError(req, 500, "Failed to configure strip");
    return;
  }

  // Persist
  _storage->saveLedConfig(*_ledEngine);

  JsonDocument resp;
  resp["message"] = "Strip configured";
  resp["id"] = stripIdx;
  _sendJson(req, 200, resp);
}

void WebServerManager::_handleDeleteLedStrip(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  if (id >= LED_MAX_STRIPS) {
    _sendError(req, 400, "Invalid strip id");
    return;
  }

  _ledEngine->removeStrip(id);
  _storage->saveLedConfig(*_ledEngine);

  JsonDocument doc;
  doc["message"] = "Strip removed";
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleTestLedStrip(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (!_parseJsonBody(req, data, len, doc)) return;

  uint8_t stripIdx = doc["id"] | 0xFF;
  if (stripIdx >= LED_MAX_STRIPS) {
    _sendError(req, 400, "Invalid strip id");
    return;
  }

  _ledEngine->testStrip(stripIdx);

  JsonDocument resp;
  resp["message"] = "Strip test triggered";
  _sendJson(req, 200, resp);
}

// ============================================================================
// Segments
// ============================================================================

void WebServerManager::_handleGetLedSegments(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray arr = doc["segments"].to<JsonArray>();

  for (uint8_t i = 0; i < _ledEngine->getSegmentCount(); i++) {
    LedSegmentConfig* seg = _ledEngine->getSegmentByIndex(i);
    if (seg) {
      JsonObject obj = arr.add<JsonObject>();
      segmentConfigToJson(*seg, obj);
    }
  }

  _sendJson(req, 200, doc);
}

void WebServerManager::_handleCreateLedSegment(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (!_parseJsonBody(req, data, len, doc)) return;

  LedSegmentConfig seg;
  segmentConfigFromJson(doc.as<JsonObject>(), seg);
  seg.id = 0; // Auto-assign

  int idx = _ledEngine->addSegment(seg);
  if (idx < 0) {
    _sendError(req, 400, "Max segments reached");
    return;
  }

  _storage->saveLedConfig(*_ledEngine);

  JsonDocument resp;
  resp["message"] = "Segment created";
  resp["id"] = _ledEngine->getSegmentByIndex(idx)->id;
  _sendJson(req, 201, resp);
}

void WebServerManager::_handleUpdateLedSegment(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  uint8_t id = _extractId(req, "id");

  JsonDocument doc;
  if (!_parseJsonBody(req, data, len, doc)) return;

  LedSegmentConfig seg;
  segmentConfigFromJson(doc.as<JsonObject>(), seg);

  if (!_ledEngine->updateSegment(id, seg)) {
    _sendError(req, 404, "Segment not found");
    return;
  }

  _storage->saveLedConfig(*_ledEngine);

  JsonDocument resp;
  resp["message"] = "Segment updated";
  _sendJson(req, 200, resp);
}

void WebServerManager::_handleDeleteLedSegment(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");

  if (!_ledEngine->removeSegment(id)) {
    _sendError(req, 404, "Segment not found");
    return;
  }

  _storage->saveLedConfig(*_ledEngine);

  JsonDocument doc;
  doc["message"] = "Segment deleted";
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleTestLedSegment(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (!_parseJsonBody(req, data, len, doc)) return;

  uint8_t segId = doc["id"] | 0xFF;
  uint8_t brightness = doc["brightness"] | 255;

  _ledEngine->testSegment(segId, brightness);

  JsonDocument resp;
  resp["message"] = "Segment test triggered";
  _sendJson(req, 200, resp);
}

// ============================================================================
// Themes
// ============================================================================

void WebServerManager::_handleGetLedThemes(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["activeThemeId"] = _ledEngine->getActiveThemeId();
  JsonArray arr = doc["themes"].to<JsonArray>();

  for (uint8_t i = 0; i < _ledEngine->getThemeCount(); i++) {
    LedThemeConfig* theme = _ledEngine->getThemeByIndex(i);
    if (theme) {
      JsonObject obj = arr.add<JsonObject>();
      themeConfigToJson(*theme, obj);
    }
  }

  _sendJson(req, 200, doc);
}

void WebServerManager::_handleCreateLedTheme(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (!_parseJsonBody(req, data, len, doc)) return;

  LedThemeConfig theme;
  themeConfigFromJson(doc.as<JsonObject>(), theme);
  theme.id = 0; // Auto-assign

  int idx = _ledEngine->addTheme(theme);
  if (idx < 0) {
    _sendError(req, 400, "Max themes reached");
    return;
  }

  _storage->saveLedThemes(*_ledEngine);

  JsonDocument resp;
  resp["message"] = "Theme created";
  resp["id"] = _ledEngine->getThemeByIndex(idx)->id;
  _sendJson(req, 201, resp);
}

void WebServerManager::_handleUpdateLedTheme(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  uint8_t id = _extractId(req, "id");

  JsonDocument doc;
  if (!_parseJsonBody(req, data, len, doc)) return;

  LedThemeConfig theme;
  themeConfigFromJson(doc.as<JsonObject>(), theme);

  if (!_ledEngine->updateTheme(id, theme)) {
    _sendError(req, 404, "Theme not found");
    return;
  }

  _storage->saveLedThemes(*_ledEngine);

  JsonDocument resp;
  resp["message"] = "Theme updated";
  _sendJson(req, 200, resp);
}

void WebServerManager::_handleDeleteLedTheme(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");

  if (!_ledEngine->removeTheme(id)) {
    _sendError(req, 404, "Theme not found");
    return;
  }

  _storage->saveLedThemes(*_ledEngine);

  JsonDocument doc;
  doc["message"] = "Theme deleted";
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleSetActiveLedTheme(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (!_parseJsonBody(req, data, len, doc)) return;

  uint8_t themeId = doc["id"] | 0xFF;
  _ledEngine->setActiveTheme(themeId);
  _storage->saveLedThemes(*_ledEngine);

  JsonDocument resp;
  resp["message"] = "Active theme set";
  resp["activeThemeId"] = themeId;
  _sendJson(req, 200, resp);
}

// ============================================================================
// Master status / brightness
// ============================================================================

void WebServerManager::_handleGetLedStatus(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["masterBrightness"] = _ledEngine->getMasterBrightness();
  doc["activeThemeId"] = _ledEngine->getActiveThemeId();
  doc["stripCount"] = _ledEngine->getActiveStripCount();
  doc["segmentCount"] = _ledEngine->getSegmentCount();
  doc["themeCount"] = _ledEngine->getThemeCount();
  doc["eventQueueOverflow"] = _ledEngine->getEventQueue().getOverflowCount();
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleSetLedBrightness(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (!_parseJsonBody(req, data, len, doc)) return;

  uint8_t brightness = doc["brightness"] | 128;
  _ledEngine->setMasterBrightness(brightness);

  JsonDocument resp;
  resp["message"] = "Brightness set";
  resp["brightness"] = brightness;
  _sendJson(req, 200, resp);
}
