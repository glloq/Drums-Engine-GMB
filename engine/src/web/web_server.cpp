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

  _server.on("/api/instruments/test-action", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handleTestInstrumentAction(req, data, len);
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

  _server.on("/api/templates", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetTemplates(req); });

  _server.on("/api/instruments/from-template", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handleCreateInstrumentFromTemplate(req, data, len);
    });

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
  const char* errMsg = nullptr;
  if (!_validateInstrumentConfig(inst, errMsg)) {
    _sendError(req, 400, errMsg ? errMsg : "Invalid instrument config");
    return;
  }

  int idx = _instrMgr->addInstrument(inst);

  if (idx < 0) { _sendError(req, 507, "Max instruments reached"); return; }

  _recompileLookupFromInstruments();
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
  const char* errMsg = nullptr;
  if (!_validateInstrumentConfig(inst, errMsg)) {
    _sendError(req, 400, errMsg ? errMsg : "Invalid instrument config");
    return;
  }

  if (!_instrMgr->updateInstrument(id, inst)) {
    _sendError(req, 404, "Instrument not found"); return;
  }

  _recompileLookupFromInstruments();
  _storage->saveInstruments(*_instrMgr);
  _sendError(req, 200, "Updated");
}

void WebServerManager::_handleDeleteInstrument(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  if (!_instrMgr->removeInstrument(id)) {
    _sendError(req, 404, "Instrument not found"); return;
  }
  _recompileLookupFromInstruments();
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

void WebServerManager::_handleTestInstrumentAction(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) {
    _sendError(req, 400, "Invalid JSON");
    return;
  }

  uint8_t id = doc["id"] | 0xFF;
  const char* eventType = doc["event"] | "on";
  uint8_t index = doc["index"] | 0;
  uint8_t velocity = doc["velocity"] | 80;

  InstrumentConfig* inst = _instrMgr->getInstrument(id);
  if (!inst) {
    _sendError(req, 404, "Instrument not found");
    return;
  }

  const ActionStep* steps = nullptr;
  uint8_t count = 0;
  if (strcmp(eventType, "off") == 0) {
    steps = inst->noteOffActions;
    count = inst->noteOffCount;
  } else {
    steps = inst->noteOnActions;
    count = inst->noteOnCount;
  }

  if (index >= count) {
    _sendError(req, 400, "Action index out of range");
    return;
  }

  bool ok = _scheduler->scheduleActionSteps(&steps[index], 1, velocity, micros(), nullptr);
  if (!ok) {
    _sendError(req, 507, "Scheduler queue full");
    return;
  }

  _sendError(req, 200, "Action test scheduled");
}

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
  obj["cc_route_count"] = _eventProc->getLookup().cc_route_count;
  obj["cc_route_dropped"] = _eventProc->getLookup().cc_route_dropped;

  const auto& ccStats = _eventProc->getCcDispatchStats();
  obj["cc_rx_total"] = ccStats.received;
  obj["cc_throttled_total"] = ccStats.throttled;
  obj["cc_routed_batches_total"] = ccStats.routed_batches;
  obj["cc_routed_commands_total"] = ccStats.routed_commands;
  obj["cc_unrouted_total"] = ccStats.unrouted;

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
  {
    JsonObject t = types.add<JsonObject>();
    t["id"] = (uint8_t)ActuatorType::MOTOR_OPTICAL;
    t["name"] = "Motor Optical";
    t["recommendedBus"] = (uint8_t)HardwareBus::GPIO_DIRECT;
    t["supportedBusMask"] = (1 << (uint8_t)HardwareBus::GPIO_DIRECT);
  }

  JsonArray behaviors = doc["actuatorBehaviors"].to<JsonArray>();
  for (uint8_t i = 0; i < (uint8_t)ActuatorBehavior::BEHAVIOR_COUNT; i++) {
    JsonObject b = behaviors.add<JsonObject>();
    b["id"] = i;
    b["name"] = actuatorBehaviorName((ActuatorBehavior)i);
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
  doc["dropped"] = lookup.cc_route_dropped;

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

void WebServerManager::_handleGetTemplates(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  JsonObject t1 = arr.add<JsonObject>();
  t1["id"] = "kick";
  t1["name"] = "Kick simple";
  t1["midiNote"] = 36;
  t1["category"] = "drums";
  t1["actuatorSlots"] = 1;
  JsonArray kickHints = t1["slotHints"].to<JsonArray>();
  JsonObject h1 = kickHints.add<JsonObject>();
  h1["slot"] = 0;
  h1["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h1["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;

  JsonObject t2 = arr.add<JsonObject>();
  t2["id"] = "hihat";
  t2["name"] = "Hi-Hat (CC4 + servo)";
  t2["midiNote"] = 42;
  t2["category"] = "cymbals";
  t2["actuatorSlots"] = 2;
  JsonArray hihatHints = t2["slotHints"].to<JsonArray>();
  JsonObject h2a = hihatHints.add<JsonObject>();
  h2a["slot"] = 0;
  h2a["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h2a["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;
  JsonObject h2b = hihatHints.add<JsonObject>();
  h2b["slot"] = 1;
  h2b["requiredType"] = (uint8_t)ActuatorType::SERVO;
  h2b["requiredBehavior"] = (uint8_t)ActuatorBehavior::SERVO_POSITION;

  JsonObject t3 = arr.add<JsonObject>();
  t3["id"] = "cymbal_mute";
  t3["name"] = "Cymbal + mute";
  t3["midiNote"] = 49;
  t3["category"] = "cymbals";
  t3["actuatorSlots"] = 2;
  JsonArray cymbalHints = t3["slotHints"].to<JsonArray>();
  JsonObject h3a = cymbalHints.add<JsonObject>();
  h3a["slot"] = 0;
  h3a["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h3a["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;
  JsonObject h3b = cymbalHints.add<JsonObject>();
  h3b["slot"] = 1;
  h3b["requiredType"] = (uint8_t)ActuatorType::SERVO;
  h3b["requiredBehavior"] = (uint8_t)ActuatorBehavior::SERVO_POSITION;

  JsonObject t4 = arr.add<JsonObject>();
  t4["id"] = "double_hit";
  t4["name"] = "Tom double frappe";
  t4["midiNote"] = 45;
  t4["category"] = "drums";
  t4["actuatorSlots"] = 2;
  JsonArray tomHints = t4["slotHints"].to<JsonArray>();
  JsonObject h4a = tomHints.add<JsonObject>();
  h4a["slot"] = 0;
  h4a["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h4a["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;
  JsonObject h4b = tomHints.add<JsonObject>();
  h4b["slot"] = 1;
  h4b["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h4b["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;

  JsonObject t5 = arr.add<JsonObject>();
  t5["id"] = "shaker";
  t5["name"] = "Shaker / Maracas";
  t5["midiNote"] = 82;
  t5["category"] = "latin";
  t5["actuatorSlots"] = 1;
  JsonArray shakerHints = t5["slotHints"].to<JsonArray>();
  JsonObject h5 = shakerHints.add<JsonObject>();
  h5["slot"] = 0;
  h5["requiredType"] = (uint8_t)ActuatorType::PWM_MOTOR;

  JsonObject t6 = arr.add<JsonObject>();
  t6["id"] = "brush";
  t6["name"] = "Brush (Comb)";
  t6["midiNote"] = 38;
  t6["category"] = "drums";
  t6["actuatorSlots"] = 1;
  JsonArray brushHints = t6["slotHints"].to<JsonArray>();
  JsonObject h6 = brushHints.add<JsonObject>();
  h6["slot"] = 0;
  h6["requiredType"] = (uint8_t)ActuatorType::PWM_MOTOR;

  JsonObject t7 = arr.add<JsonObject>();
  t7["id"] = "timpani";
  t7["name"] = "Timpani pitch";
  t7["midiNote"] = 47;
  t7["category"] = "drums";
  t7["actuatorSlots"] = 2;
  JsonArray timpaniHints = t7["slotHints"].to<JsonArray>();
  JsonObject h7a = timpaniHints.add<JsonObject>();
  h7a["slot"] = 0;
  h7a["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h7a["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;
  JsonObject h7b = timpaniHints.add<JsonObject>();
  h7b["slot"] = 1;
  h7b["requiredType"] = (uint8_t)ActuatorType::SERVO;
  h7b["requiredBehavior"] = (uint8_t)ActuatorBehavior::PITCH_BEND;
  JsonObject t8 = arr.add<JsonObject>();
  t8["id"] = "motor_optical";
  t8["name"] = "Motor + capteur optique";
  t8["midiNote"] = 60;
  t8["category"] = "other";
  t8["actuatorSlots"] = 1;
  JsonArray hints = t8["slotHints"].to<JsonArray>();
  JsonObject h0 = hints.add<JsonObject>();
  h0["slot"] = 0;
  h0["requiredType"] = (uint8_t)ActuatorType::MOTOR_OPTICAL;
  h0["requiredBehavior"] = (uint8_t)ActuatorBehavior::MOTOR_OPTICAL_TRACK;


  _sendJson(req, 200, doc);
}

void WebServerManager::_handleCreateInstrumentFromTemplate(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) {
    _sendError(req, 400, "Invalid JSON");
    return;
  }

  const char* tpl = doc["template"] | "";
  if (!tpl || tpl[0] == '\0') {
    _sendError(req, 400, "template required");
    return;
  }

  uint8_t requestedActuators[3] = {0xFF, 0xFF, 0xFF};
  JsonArray reqActuatorIds = doc["actuatorIds"].as<JsonArray>();
  if (!reqActuatorIds.isNull()) {
    uint8_t idx = 0;
    for (JsonVariant v : reqActuatorIds) {
      if (idx >= 3) break;
      requestedActuators[idx++] = v | 0xFF;
    }
  }
  requestedActuators[0] = doc["actuatorId"] | requestedActuators[0];
  requestedActuators[0] = doc["primaryActuatorId"] | requestedActuators[0];
  requestedActuators[1] = doc["secondaryActuatorId"] | requestedActuators[1];
  requestedActuators[2] = doc["tertiaryActuatorId"] | requestedActuators[2];

  auto validateTemplateSlot = [this](uint8_t actuatorId,
                                     ActuatorType requiredType,
                                     ActuatorBehavior requiredBehavior,
                                     const char*& errMsg) -> bool {
    if (actuatorId == 0xFF) {
      errMsg = "Template requires actuatorIds mapping";
      return false;
    }
    ActuatorConfig* cfg = _actFactory->findConfig(actuatorId);
    if (!cfg) {
      errMsg = "Template slot references unknown actuatorId";
      return false;
    }
    if (cfg->type != requiredType) {
      errMsg = "Template slot actuator type mismatch";
      return false;
    }
    if ((uint8_t)requiredBehavior < (uint8_t)ActuatorBehavior::BEHAVIOR_COUNT &&
        cfg->behavior != requiredBehavior) {
      errMsg = "Template slot actuator behavior mismatch";
      return false;
    }
    return true;
  };

  const char* templateErr = nullptr;
  auto requireSlot = [&](uint8_t slot,
                         ActuatorType type,
                         ActuatorBehavior behavior) -> bool {
    if (slot >= 3) {
      templateErr = "Invalid template slot index";
      return false;
    }
    return validateTemplateSlot(requestedActuators[slot], type, behavior, templateErr);
  };

  if (strcmp(tpl, "kick") == 0) {
    if (!requireSlot(0, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "hihat") == 0) {
    if (!requireSlot(0, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE) ||
        !requireSlot(1, ActuatorType::SERVO, ActuatorBehavior::SERVO_POSITION)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "cymbal_mute") == 0) {
    if (!requireSlot(0, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE) ||
        !requireSlot(1, ActuatorType::SERVO, ActuatorBehavior::SERVO_POSITION)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "double_hit") == 0) {
    if (!requireSlot(0, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE) ||
        !requireSlot(1, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "shaker") == 0) {
    if (!requireSlot(0, ActuatorType::PWM_MOTOR, ActuatorBehavior::BEHAVIOR_COUNT)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "brush") == 0) {
    if (!requireSlot(0, ActuatorType::PWM_MOTOR, ActuatorBehavior::BEHAVIOR_COUNT)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "timpani") == 0) {
    if (!requireSlot(0, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE) ||
        !requireSlot(1, ActuatorType::SERVO, ActuatorBehavior::PITCH_BEND)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "motor_optical") == 0) {
    if (!requireSlot(0, ActuatorType::MOTOR_OPTICAL, ActuatorBehavior::MOTOR_OPTICAL_TRACK)) {
      _sendError(req, 400, templateErr);
      return;
    }
  }

  InstrumentConfig inst;
  strlcpy(inst.name, doc["name"] | "Template Instrument", sizeof(inst.name));
  strlcpy(inst.category, doc["category"] | "drums", sizeof(inst.category));
  inst.midiNote = doc["midiNote"] | 36;
  inst.midiChannel = doc["midiChannel"] | 10;
  inst.enabled = true;

  if (strcmp(tpl, "kick") == 0) {
    strlcpy(inst.name, "Kick simple", sizeof(inst.name));
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].duration_min_ms = 8;
    inst.noteOnActions[0].duration_max_ms = 25;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.noteOffCount = 1;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].actuator_id = requestedActuators[0];
  } else if (strcmp(tpl, "hihat") == 0) {
    strlcpy(inst.name, "Hi-Hat", sizeof(inst.name));
    strlcpy(inst.category, "cymbals", sizeof(inst.category));
    inst.noteOnCount = 2;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].duration_min_ms = 8;
    inst.noteOnActions[0].duration_max_ms = 25;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.noteOnActions[1].command_type = (uint8_t)CommandType::POSITION;
    inst.noteOnActions[1].value_source = (uint8_t)ValueSource::CC_VAR;
    inst.noteOnActions[1].cc_index = 4;
    inst.noteOnActions[1].actuator_id = requestedActuators[1];

    inst.ccBindingCount = 1;
    inst.ccBindings[0].cc_number = 4;
    inst.ccBindings[0].command_type = (uint8_t)CommandType::POSITION;
    inst.ccBindings[0].range_min = 30;
    inst.ccBindings[0].range_max = 150;
    inst.ccBindings[0].actuator_id = requestedActuators[1];
  } else if (strcmp(tpl, "cymbal_mute") == 0) {
    strlcpy(inst.name, "Cymbal + mute", sizeof(inst.name));
    strlcpy(inst.category, "cymbals", sizeof(inst.category));
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].duration_min_ms = 8;
    inst.noteOnActions[0].duration_max_ms = 20;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];

    inst.noteOffCount = 2;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::POSITION;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].value_fixed = 20;
    inst.noteOffActions[0].delay_ms = 50;
    inst.noteOffActions[0].actuator_id = requestedActuators[1];
    inst.noteOffActions[1].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[1].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[1].actuator_id = requestedActuators[0];
  } else if (strcmp(tpl, "double_hit") == 0) {
    strlcpy(inst.name, "Tom double frappe", sizeof(inst.name));
    strlcpy(inst.category, "drums", sizeof(inst.category));
    inst.noteOnCount = 2;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].duration_min_ms = 8;
    inst.noteOnActions[0].duration_max_ms = 22;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.noteOnActions[1] = inst.noteOnActions[0];
    inst.noteOnActions[1].actuator_id = requestedActuators[1];
    inst.noteOffCount = 1;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].actuator_id = requestedActuators[0];
  } else if (strcmp(tpl, "shaker") == 0) {
    strlcpy(inst.name, "Shaker / Maracas", sizeof(inst.name));
    strlcpy(inst.category, "latin", sizeof(inst.category));
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PWM;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.noteOffCount = 1;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].actuator_id = requestedActuators[0];
  } else if (strcmp(tpl, "brush") == 0) {
    strlcpy(inst.name, "Brush", sizeof(inst.name));
    strlcpy(inst.category, "drums", sizeof(inst.category));
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PWM;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.ccBindingCount = 1;
    inst.ccBindings[0].cc_number = 1;
    inst.ccBindings[0].command_type = (uint8_t)CommandType::PWM;
    inst.ccBindings[0].range_min = 20;
    inst.ccBindings[0].range_max = 127;
    inst.ccBindings[0].actuator_id = requestedActuators[0];
    inst.noteOffCount = 1;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].actuator_id = requestedActuators[0];
  } else if (strcmp(tpl, "timpani") == 0) {
    strlcpy(inst.name, "Timpani", sizeof(inst.name));
    strlcpy(inst.category, "drums", sizeof(inst.category));
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].duration_min_ms = 10;
    inst.noteOnActions[0].duration_max_ms = 30;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.ccBindingCount = 1;
    inst.ccBindings[0].cc_number = 127;
    inst.ccBindings[0].command_type = (uint8_t)CommandType::POSITION;
    inst.ccBindings[0].range_min = 30;
    inst.ccBindings[0].range_max = 110;
    inst.ccBindings[0].actuator_id = (requestedActuators[1] == 0xFF) ? requestedActuators[0] : requestedActuators[1];
  } else if (strcmp(tpl, "motor_optical") == 0) {
    strlcpy(inst.name, "Motor Optical", sizeof(inst.name));
    strlcpy(inst.category, "other", sizeof(inst.category));
    inst.midiNote = doc["midiNote"] | 60;
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::POSITION;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.noteOffCount = 1;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].actuator_id = requestedActuators[0];
  } else {
    _sendError(req, 400, "Unknown template");
    return;
  }

  const char* errMsg = nullptr;
  if (!_validateInstrumentConfig(inst, errMsg)) {
    _sendError(req, 400, errMsg ? errMsg : "Invalid template instrument config");
    return;
  }

  int idx = _instrMgr->addInstrument(inst);
  if (idx < 0) {
    _sendError(req, 507, "Max instruments reached");
    return;
  }

  _recompileLookupFromInstruments();
  _storage->saveInstruments(*_instrMgr);

  JsonDocument resp;
  JsonObject obj = resp.to<JsonObject>();
  _instrMgr->instrumentToJson(*_instrMgr->getInstrumentByIndex(idx), obj);
  _sendJson(req, 201, resp);
}

void WebServerManager::_recompileLookupFromInstruments() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  _instrMgr->toJson(arr);
  PipelineCompiler::compileAll(arr, _eventProc->getLookup());
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


bool WebServerManager::_validateInstrumentConfig(InstrumentConfig& cfg, const char*& errMsg) {
  if (cfg.midiNote >= 128) {
    errMsg = "midiNote must be in [0..127]";
    return false;
  }

  if (cfg.midiChannel > 16) {
    errMsg = "midiChannel must be 0 (all) or [1..16]";
    return false;
  }

  if (cfg.noteOnCount > MAX_ACTIONS_PER_EVENT || cfg.noteOffCount > MAX_ACTIONS_PER_EVENT) {
    errMsg = "Too many action steps";
    return false;
  }

  if (cfg.ccBindingCount > MAX_CC_BINDINGS) {
    errMsg = "Too many CC bindings";
    return false;
  }

  auto actuatorConfig = [this](uint8_t actuatorId) -> ActuatorConfig* {
    if (actuatorId == 0xFF) return nullptr;
    return _actFactory->findConfig(actuatorId);
  };

  auto commandCompatibleWithActuator = [&](uint8_t commandType, const ActuatorConfig& actCfg) -> bool {
    switch ((CommandType)commandType) {
      case CommandType::PULSE:
        return actCfg.type == ActuatorType::SOLENOID;
      case CommandType::POSITION:
        return actCfg.type == ActuatorType::SERVO || actCfg.type == ActuatorType::MOTOR_OPTICAL;
      case CommandType::PWM:
        return actCfg.type == ActuatorType::PWM_MOTOR || actCfg.type == ActuatorType::MOTOR_OPTICAL;
      case CommandType::OFF:
        return true;
      default:
        return false;
    }
  };

  auto normalizeAndValidateStep = [&](ActionStep& step, const char* label) -> bool {
    if (step.command_type > (uint8_t)CommandType::OFF) {
      errMsg = (strcmp(label, "noteOnActions") == 0)
        ? "Invalid command_type in noteOnActions"
        : "Invalid command_type in noteOffActions";
      return false;
    }

    if (step.value_source > (uint8_t)ValueSource::INVERTED_VELOCITY) {
      errMsg = (strcmp(label, "noteOnActions") == 0)
        ? "Invalid value_source in noteOnActions"
        : "Invalid value_source in noteOffActions";
      return false;
    }

    if (step.velocity_curve > CURVE_LOG) {
      errMsg = "velocity_curve must be in [CURVE_LINEAR..CURVE_LOG]";
      return false;
    }

    ActuatorConfig* actCfg = actuatorConfig(step.actuator_id);
    if (!actCfg) {
      errMsg = "Action step references unknown actuatorId";
      return false;
    }

    if (!commandCompatibleWithActuator(step.command_type, *actCfg)) {
      errMsg = "Action commandType incompatible with actuator type";
      return false;
    }

    if (step.value_source == (uint8_t)ValueSource::CC_VAR && step.cc_index >= MAX_GLOBAL_VARS) {
      errMsg = "cc_index out of range for CC_VAR source";
      return false;
    }

    if (step.duration_max_ms < step.duration_min_ms) {
      uint16_t t = step.duration_min_ms;
      step.duration_min_ms = step.duration_max_ms;
      step.duration_max_ms = t;
    }

    return true;
  };

  for (uint8_t i = 0; i < cfg.noteOnCount; i++) {
    if (!normalizeAndValidateStep(cfg.noteOnActions[i], "noteOnActions")) {
      return false;
    }
  }

  for (uint8_t i = 0; i < cfg.noteOffCount; i++) {
    if (!normalizeAndValidateStep(cfg.noteOffActions[i], "noteOffActions")) {
      return false;
    }
  }

  for (uint8_t i = 0; i < cfg.ccBindingCount; i++) {
    CCBinding& b = cfg.ccBindings[i];
    if (b.cc_number >= 128) {
      errMsg = "ccNumber must be in [0..127]";
      return false;
    }

    ActuatorConfig* actCfg = actuatorConfig(b.actuator_id);
    if (!actCfg) {
      errMsg = "ccBinding references unknown actuatorId";
      return false;
    }

    if (b.command_type != (uint8_t)CommandType::POSITION && b.command_type != (uint8_t)CommandType::PWM) {
      errMsg = "ccBindings.commandType must be POSITION or PWM";
      return false;
    }

    if (!commandCompatibleWithActuator(b.command_type, *actCfg)) {
      errMsg = "ccBinding commandType incompatible with actuator type";
      return false;
    }

    if (b.curve > CURVE_LOG) {
      errMsg = "ccBindings.curve must be in [CURVE_LINEAR..CURVE_LOG]";
      return false;
    }

    if (b.range_max < b.range_min) {
      uint8_t t = b.range_min;
      b.range_min = b.range_max;
      b.range_max = t;
    }

    for (uint8_t j = i + 1; j < cfg.ccBindingCount; j++) {
      const CCBinding& other = cfg.ccBindings[j];
      if (other.cc_number == b.cc_number &&
          other.actuator_id == b.actuator_id &&
          other.command_type == b.command_type) {
        errMsg = "Duplicate ccBinding (ccNumber + actuatorId + commandType)";
        return false;
      }
    }
  }

  errMsg = nullptr;
  return true;
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

  if (cfg.type == ActuatorType::MOTOR_OPTICAL) {
    if (cfg.bus != HardwareBus::GPIO_DIRECT) {
      errMsg = "Motor optical currently requires GPIO_DIRECT (sensor read not available on PWM-only drivers)";
      return false;
    }
    if (cfg.behavior != ActuatorBehavior::MOTOR_OPTICAL_TRACK) {
      errMsg = "Motor optical requires behavior MOTOR_OPTICAL_TRACK";
      return false;
    }
    if (cfg.hwAddress > 39) {
      errMsg = "Motor optical sensor pin (hwAddress) must be in [0..39]";
      return false;
    }
    if (cfg.hwPin > 39) {
      errMsg = "Motor optical drive pin (hwPin) must be in [0..39]";
      return false;
    }
    if (cfg.hwPin == cfg.hwAddress) {
      errMsg = "Motor optical drive pin and sensor pin must be different";
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
