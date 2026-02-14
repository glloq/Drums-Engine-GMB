#include "web_server.h"
#include "../hal/i2c_scanner.h"

WebServerManager::WebServerManager(InstrumentManager* instrMgr, ActuatorManager* actMgr,
                                    ActuatorFactory* actFactory,
                                    LoopEngine* loopEngine, MidiEngine* midiEngine,
                                    Storage* storage, Scheduler* scheduler,
                                    EventProcessor* eventProc)
  : _server(WEB_PORT), _ws(WS_PATH),
    _instrMgr(instrMgr), _actMgr(actMgr), _actFactory(actFactory),
    _loopEngine(loopEngine),
    _midiEngine(midiEngine), _storage(storage), _scheduler(scheduler),
    _eventProc(eventProc) {}

bool WebServerManager::begin() {
  _setupRoutes();

  _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                     AwsEventType type, void* arg, uint8_t* data, size_t len) {
    _onWsEvent(server, client, type, arg, data, len);
  });
  _server.addHandler(&_ws);

  _server.begin();
  DBGF("[Web] Server started on port %d\n", WEB_PORT);
  return true;
}

void WebServerManager::update() {
  _ws.cleanupClients();
}

void WebServerManager::_setupRoutes() {
  // UI web depuis LittleFS
  _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(LittleFS, "/index.html", "text/html");
  });
  _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // --- Instruments API ---
  _server.on("/api/instruments", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetInstruments(req); });

  _server.on("/api/instrument", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetInstrument(req); });

  _server.on("/api/instruments", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handleCreateInstrument(req, data, len);
    });

  _server.on("/api/instrument", HTTP_PUT,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handleUpdateInstrument(req, data, len);
    });

  _server.on("/api/instrument", HTTP_DELETE,
    [this](AsyncWebServerRequest* req) { _handleDeleteInstrument(req); });

  // --- Actuators API ---
  _server.on("/api/actuators", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetActuators(req); });

  _server.on("/api/actuators", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handleCreateActuator(req, data, len);
    });

  _server.on("/api/actuator", HTTP_PUT,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handleUpdateActuator(req, data, len);
    });

  _server.on("/api/actuator", HTTP_DELETE,
    [this](AsyncWebServerRequest* req) { _handleDeleteActuator(req); });

  // --- Test API ---
  _server.on("/api/test/instrument", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handleTestInstrument(req, data, len);
    });

  _server.on("/api/test/actuator", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handleTestActuator(req, data, len);
    });

  // --- Loops API ---
  _server.on("/api/loops", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetLoops(req); });

  _server.on("/api/loop", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetLoop(req); });

  _server.on("/api/loops", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handleCreateLoop(req, data, len);
    });

  _server.on("/api/loop", HTTP_PUT,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handleUpdateLoop(req, data, len);
    });

  _server.on("/api/loop", HTTP_DELETE,
    [this](AsyncWebServerRequest* req) { _handleDeleteLoop(req); });

  _server.on("/api/loop/play", HTTP_POST,
    [this](AsyncWebServerRequest* req) { _handlePlayLoop(req); });

  _server.on("/api/loop/stop", HTTP_POST,
    [this](AsyncWebServerRequest* req) { _handleStopLoop(req); });

  // --- System API ---
  _server.on("/api/status", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetStatus(req); });

  _server.on("/api/scan-i2c", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleScanI2C(req); });

  _server.on("/api/midi-notes", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetMidiNotes(req); });

  _server.on("/api/capabilities", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetCapabilities(req); });

  _server.on("/api/cc-routes", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetCcRoutes(req); });

  _server.on("/api/save", HTTP_POST,
    [this](AsyncWebServerRequest* req) { _handleSaveConfig(req); });

  _server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "application/json", "{\"error\":\"Not found\"}");
  });
}

// --- Instruments ---

void WebServerManager::_handleGetInstruments(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  _instrMgr->toJson(arr);
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleGetInstrument(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  InstrumentConfig* inst = _instrMgr->getInstrument(id);
  if (!inst) { _sendError(req, 404, "Instrument not found"); return; }

  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  _instrMgr->instrumentToJson(*inst, obj);
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleCreateInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }

  InstrumentConfig inst;
  _instrMgr->instrumentFromJson(doc.as<JsonObject>(), inst);
  int idx = _instrMgr->addInstrument(inst);

  if (idx < 0) { _sendError(req, 507, "Max instruments reached"); return; }

  _storage->saveInstruments(*_instrMgr);

  JsonDocument resp;
  JsonObject obj = resp.to<JsonObject>();
  _instrMgr->instrumentToJson(*_instrMgr->getInstrumentByIndex(idx), obj);
  _sendJson(req, 201, resp);
}

void WebServerManager::_handleUpdateInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  uint8_t id = _extractId(req, "id");
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }

  InstrumentConfig inst;
  _instrMgr->instrumentFromJson(doc.as<JsonObject>(), inst);

  if (!_instrMgr->updateInstrument(id, inst)) {
    _sendError(req, 404, "Instrument not found"); return;
  }

  _storage->saveInstruments(*_instrMgr);
  _sendError(req, 200, "Updated");
}

void WebServerManager::_handleDeleteInstrument(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  if (!_instrMgr->removeInstrument(id)) {
    _sendError(req, 404, "Instrument not found"); return;
  }
  _storage->saveInstruments(*_instrMgr);
  _sendError(req, 200, "Deleted");
}

// --- Actuators ---

void WebServerManager::_handleGetActuators(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < _actFactory->getConfigCount(); i++) {
    const ActuatorConfig& cfg = _actFactory->getConfig(i);
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = cfg.id;
    obj["name"] = cfg.name;
    obj["type"] = (uint8_t)cfg.type;
    obj["typeName"] = actuatorTypeName(cfg.type);
    obj["behavior"] = (uint8_t)cfg.behavior;
    obj["behaviorName"] = actuatorBehaviorName(cfg.behavior);
    obj["bus"] = (uint8_t)cfg.bus;
    obj["busName"] = hardwareBusName(cfg.bus);
    obj["hwAddress"] = cfg.hwAddress;
    obj["hwPin"] = cfg.hwPin;
    obj["paramMin"] = cfg.paramMin;
    obj["paramMax"] = cfg.paramMax;
    obj["paramDefault"] = cfg.paramDefault;
    obj["cooldownUs"] = cfg.cooldownUs;
    obj["enabled"] = cfg.enabled;
    obj["inverted"] = cfg.inverted;

    // Etat temps reel
    Actuator* act = _actMgr->getActuator(cfg.id);
    obj["active"] = act ? act->isActive() : false;
  }
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleCreateActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }

  ActuatorConfig cfg;
  cfg.id = doc["id"] | 0;
  strlcpy(cfg.name, doc["name"] | "Actuator", sizeof(cfg.name));
  cfg.type = (ActuatorType)(doc["type"] | 0);
  cfg.behavior = (ActuatorBehavior)(doc["behavior"] | 0);
  cfg.bus = (HardwareBus)(doc["bus"] | 0);
  cfg.hwAddress = doc["hwAddress"] | 0x20;
  cfg.hwPin = doc["hwPin"] | 0;
  cfg.paramMin = doc["paramMin"] | 8;
  cfg.paramMax = doc["paramMax"] | 30;
  cfg.paramDefault = doc["paramDefault"] | 15;
  cfg.cooldownUs = doc["cooldownUs"] | 200;
  cfg.enabled = doc["enabled"] | true;
  cfg.inverted = doc["inverted"] | false;

  const char* errMsg = nullptr;
  if (!_validateActuatorConfig(cfg, errMsg)) {
    _sendError(req, 400, errMsg);
    return;
  }

  int idx = _actFactory->addConfig(cfg);
  if (idx < 0) { _sendError(req, 507, "Max actuators reached"); return; }

  // Reconstruire les actionneurs physiques
  _actFactory->rebuildAll();
  _storage->saveActuators(*_actFactory);

  JsonDocument resp;
  resp["id"] = _actFactory->getConfig(idx).id;
  resp["message"] = "Actuator created";
  _sendJson(req, 201, resp);
}

void WebServerManager::_handleUpdateActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  uint8_t id = _extractId(req, "id");
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }

  ActuatorConfig* cfg = _actFactory->findConfig(id);
  if (!cfg) { _sendError(req, 404, "Actuator config not found"); return; }

  if (doc.containsKey("name"))        strlcpy(cfg->name, doc["name"] | "Actuator", sizeof(cfg->name));
  if (doc.containsKey("type"))        cfg->type = (ActuatorType)(doc["type"] | 0);
  if (doc.containsKey("behavior"))    cfg->behavior = (ActuatorBehavior)(doc["behavior"] | 0);
  if (doc.containsKey("bus"))         cfg->bus = (HardwareBus)(doc["bus"] | 0);
  if (doc.containsKey("hwAddress"))   cfg->hwAddress = doc["hwAddress"] | 0x20;
  if (doc.containsKey("hwPin"))       cfg->hwPin = doc["hwPin"] | 0;
  if (doc.containsKey("paramMin"))    cfg->paramMin = doc["paramMin"] | 8;
  if (doc.containsKey("paramMax"))    cfg->paramMax = doc["paramMax"] | 30;
  if (doc.containsKey("paramDefault")) cfg->paramDefault = doc["paramDefault"] | 15;
  if (doc.containsKey("cooldownUs"))  cfg->cooldownUs = doc["cooldownUs"] | 200;
  if (doc.containsKey("enabled"))     cfg->enabled = doc["enabled"] | true;
  if (doc.containsKey("inverted"))    cfg->inverted = doc["inverted"] | false;

  const char* errMsg = nullptr;
  if (!_validateActuatorConfig(*cfg, errMsg)) {
    _sendError(req, 400, errMsg);
    return;
  }

  // Reconstruire les actionneurs physiques
  _actFactory->rebuildAll();
  _storage->saveActuators(*_actFactory);

  _sendError(req, 200, "Updated");
}

void WebServerManager::_handleDeleteActuator(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  if (!_actFactory->removeConfig(id)) {
    _sendError(req, 404, "Actuator config not found"); return;
  }

  _actFactory->rebuildAll();
  _storage->saveActuators(*_actFactory);
  _sendError(req, 200, "Deleted");
}

void WebServerManager::_handleTestActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }
  uint8_t id = doc["id"] | 0;
  uint8_t val = doc["value"] | 80;
  _actMgr->testActuator(id, val);
  _sendError(req, 200, "Testing");
}

// --- Testing ---

void WebServerManager::_handleTestInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }
  uint8_t id = doc["id"] | 0;
  uint8_t vel = doc["velocity"] | 80;
  _instrMgr->testInstrument(id, vel);
  _sendError(req, 200, "Testing");
}

// --- Loops ---

void WebServerManager::_handleGetLoops(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < _loopEngine->getLoopCount(); i++) {
    Loop* loop = _loopEngine->getLoop(i);
    if (loop) {
      JsonObject obj = arr.add<JsonObject>();
      _loopEngine->loopToJson(*loop, obj);
    }
  }
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleGetLoop(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  Loop* loop = _loopEngine->getLoop(id);
  if (!loop) { _sendError(req, 404, "Loop not found"); return; }

  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  _loopEngine->loopToJson(*loop, obj);
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleCreateLoop(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }

  const char* name = doc["name"] | "New Loop";
  uint16_t bpm = doc["bpm"] | MIDI_BPM_DEFAULT;
  uint8_t bars = doc["bars"] | 1;
  uint8_t beatsPerBar = doc["beatsPerBar"] | 4;
  uint8_t beatValue = doc["beatValue"] | 4;

  int idx = _loopEngine->createLoop(name, bpm, bars, beatsPerBar, beatValue);
  if (idx < 0) { _sendError(req, 507, "Max loops reached"); return; }

  JsonArray events = doc["events"].as<JsonArray>();
  if (!events.isNull()) {
    for (JsonArray evt : events) {
      _loopEngine->addEvent(idx, evt[0] | 0, evt[1] | 36, evt[2] | 80, evt[3] | 10);
    }
  }

  JsonDocument resp;
  resp["index"] = idx;
  resp["message"] = "Loop created";
  _sendJson(req, 201, resp);
}

void WebServerManager::_handleUpdateLoop(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  uint8_t id = _extractId(req, "id");
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }

  Loop* loop = _loopEngine->getLoop(id);
  if (!loop) { _sendError(req, 404, "Loop not found"); return; }

  _loopEngine->loopFromJson(doc.as<JsonObject>(), *loop);
  _sendError(req, 200, "Updated");
}

void WebServerManager::_handleDeleteLoop(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  if (!_loopEngine->deleteLoop(id)) { _sendError(req, 404, "Loop not found"); return; }
  _sendError(req, 200, "Deleted");
}

void WebServerManager::_handlePlayLoop(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  _loopEngine->play(id);
  _sendError(req, 200, "Playing");
}

void WebServerManager::_handleStopLoop(AsyncWebServerRequest* req) {
  _loopEngine->stop();
  _sendError(req, 200, "Stopped");
}

// --- System ---

void WebServerManager::_handleGetStatus(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();

  // WiFi
  obj["wifi_ssid"] = WiFi.SSID();
  obj["wifi_ip"] = WiFi.localIP().toString();
  obj["wifi_rssi"] = WiFi.RSSI();

  // MIDI
  obj["midi_connected"] = _midiEngine->isConnected();
  obj["midi_sessions"] = _midiEngine->getSessionCount();
  obj["midi_notes_rx"] = _midiEngine->getNotesReceived();
  obj["midi_notes_tx"] = _midiEngine->getNotesSent();

  // Actuators
  obj["actuator_count"] = _actMgr->getCount();
  obj["actuator_active"] = _actMgr->getActiveCount();
  obj["actuator_configs"] = _actFactory->getConfigCount();

  // Instruments
  obj["instrument_count"] = _instrMgr->getInstrumentCount();

  // Scheduler
  obj["scheduler_queue"] = _scheduler->queueCount();
  obj["scheduler_usage"] = _scheduler->queueUsage();
  obj["scheduler_dispatched"] = _scheduler->getDispatchCount();
  obj["scheduler_overflow"] = _scheduler->getOverflowCount();
  obj["scheduler_jitter_us"] = _scheduler->getLastJitterUs();
  obj["scheduler_max_jitter_us"] = _scheduler->getMaxJitterUs();

  // Event Engine
  obj["pipeline_count"] = _eventProc->getLookup().pipeline_count;

  // Loops
  obj["loop_count"] = _loopEngine->getLoopCount();
  obj["loop_playing"] = _loopEngine->isPlaying();
  if (_loopEngine->isPlaying()) {
    obj["loop_bpm"] = _loopEngine->getBpm();
    obj["loop_beat"] = _loopEngine->getCurrentBeat();
    obj["loop_bar"] = _loopEngine->getCurrentBar();
  }

  // System
  obj["free_heap"] = ESP.getFreeHeap();
  obj["uptime_s"] = millis() / 1000;
  obj["storage"] = _storage->formatInfo();

  _sendJson(req, 200, doc);
}

void WebServerManager::_handleScanI2C(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["devices"] = I2CScanner::scan();
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleGetMidiNotes(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < GM_DRUM_NOTES_COUNT; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["note"] = GM_DRUM_NOTES[i].note;
    obj["name"] = GM_DRUM_NOTES[i].name;
  }
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleGetCapabilities(AsyncWebServerRequest* req) {
  JsonDocument doc;

  JsonArray types = doc["actuatorTypes"].to<JsonArray>();
  {
    JsonObject t = types.add<JsonObject>();
    t["id"] = (uint8_t)ActuatorType::SERVO;
    t["name"] = "Servo";
    t["recommendedBus"] = (uint8_t)HardwareBus::PCA9685;
    t["supportedBusMask"] = (1 << (uint8_t)HardwareBus::PCA9685);
  }
  {
    JsonObject t = types.add<JsonObject>();
    t["id"] = (uint8_t)ActuatorType::SOLENOID;
    t["name"] = "Solenoid";
    t["recommendedBus"] = (uint8_t)HardwareBus::MCP23017;
    t["supportedBusMask"] = (1 << (uint8_t)HardwareBus::MCP23017) |
                            (1 << (uint8_t)HardwareBus::GPIO_DIRECT);
  }
  {
    JsonObject t = types.add<JsonObject>();
    t["id"] = (uint8_t)ActuatorType::PWM_MOTOR;
    t["name"] = "PWM Motor";
    t["recommendedBus"] = (uint8_t)HardwareBus::LEDC_PWM;
    t["supportedBusMask"] = (1 << (uint8_t)HardwareBus::LEDC_PWM) |
                            (1 << (uint8_t)HardwareBus::PCA9685);
  }

  doc["maxActuators"] = MAX_ACTUATORS;
  doc["maxInstruments"] = MAX_INSTRUMENTS;
  doc["maxActuatorsPerInstrument"] = MAX_ACTUATORS_PER_INST;

  _sendJson(req, 200, doc);
}

void WebServerManager::_handleGetCcRoutes(AsyncWebServerRequest* req) {
  const PipelineLookup& lookup = _eventProc->getLookup();
  JsonDocument doc;
  doc["count"] = lookup.cc_route_count;

  JsonArray routes = doc["routes"].to<JsonArray>();
  for (uint8_t cc = 0; cc < 128; cc++) {
    uint8_t first = lookup.cc_to_first[cc];
    uint8_t count = lookup.cc_to_count[cc];
    if (first == 0xFF || count == 0) continue;

    JsonObject ccObj = routes.add<JsonObject>();
    ccObj["cc"] = cc;
    JsonArray entries = ccObj["entries"].to<JsonArray>();

    for (uint8_t i = 0; i < count; i++) {
      uint8_t routeIdx = first + i;
      if (routeIdx >= lookup.cc_route_count) break;
      const CCRoutingEntry& route = lookup.cc_routes[routeIdx];

      JsonObject e = entries.add<JsonObject>();
      e["actuatorId"] = route.actuator_id;
      e["commandType"] = route.command_type;
      e["rangeMin"] = route.range_min;
      e["rangeMax"] = route.range_max;
      e["curve"] = route.curve;
      e["inverted"] = route.inverted;
    }
  }

  _sendJson(req, 200, doc);
}

void WebServerManager::_handleSaveConfig(AsyncWebServerRequest* req) {
  _storage->saveActuators(*_actFactory);
  _storage->saveInstruments(*_instrMgr);
  _sendError(req, 200, "Configuration saved");
}

// --- WebSocket ---

void WebServerManager::_onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                   AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      DBGF("[WS] Client #%u connected from %s\n", client->id(),
           client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      DBGF("[WS] Client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA: {
      AwsFrameInfo* info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        JsonDocument doc;
        if (!deserializeJson(doc, (char*)data)) {
          const char* action = doc["action"];
          if (action && strcmp(action, "ping") == 0) {
            client->text("{\"action\":\"pong\"}");
          }
        }
      }
      break;
    }
    default: break;
  }
}

void WebServerManager::_wsBroadcast(const char* type, const JsonObject& data) {
  JsonDocument doc;
  doc["type"] = type;
  doc["data"] = data;
  String msg;
  serializeJson(doc, msg);
  _ws.textAll(msg);
}

bool WebServerManager::_validateActuatorConfig(const ActuatorConfig& cfg, const char*& errMsg) {
  if (cfg.paramMin > cfg.paramMax) {
    errMsg = "paramMin must be <= paramMax";
    return false;
  }

  if (cfg.paramDefault < cfg.paramMin || cfg.paramDefault > cfg.paramMax) {
    errMsg = "paramDefault must be inside [paramMin, paramMax]";
    return false;
  }

  if (cfg.type == ActuatorType::SERVO) {
    if (cfg.bus != HardwareBus::PCA9685) {
      errMsg = "Servo currently requires PCA9685 bus";
      return false;
    }
    if (cfg.hwPin > 15) {
      errMsg = "Servo channel must be in [0..15]";
      return false;
    }
  }

  if (cfg.type == ActuatorType::SOLENOID) {
    if (cfg.bus != HardwareBus::MCP23017 && cfg.bus != HardwareBus::GPIO_DIRECT) {
      errMsg = "Solenoid supports MCP23017 or GPIO_DIRECT";
      return false;
    }
  }

  if (cfg.type == ActuatorType::PWM_MOTOR) {
    if (cfg.bus != HardwareBus::LEDC_PWM && cfg.bus != HardwareBus::PCA9685) {
      errMsg = "PWM motor supports LEDC_PWM or PCA9685";
      return false;
    }
  }

  errMsg = nullptr;
  return true;
}

// --- Helpers ---

void WebServerManager::_sendJson(AsyncWebServerRequest* req, int code, const JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  req->send(code, "application/json", json);
}

void WebServerManager::_sendError(AsyncWebServerRequest* req, int code, const char* msg) {
  JsonDocument doc;
  doc["message"] = msg;
  _sendJson(req, code, doc);
}

uint8_t WebServerManager::_extractId(AsyncWebServerRequest* req, const char* param) {
  if (req->hasParam(param)) {
    return req->getParam(param)->value().toInt();
  }
  return 0;
}
