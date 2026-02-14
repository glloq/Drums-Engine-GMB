#include "web_server.h"
#include "../hal/i2c_scanner.h"

WebServerManager::WebServerManager(InstrumentManager* instrMgr, ActuatorManager* actMgr,
                                    LoopEngine* loopEngine, MidiEngine* midiEngine,
                                    Storage* storage, Scheduler* scheduler,
                                    EventProcessor* eventProc)
  : _server(WEB_PORT), _ws(WS_PATH),
    _instrMgr(instrMgr), _actMgr(actMgr), _loopEngine(loopEngine),
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
  for (uint8_t i = 0; i < _actMgr->getCount(); i++) {
    // Iterate through registered actuators
    // This is a simplified version - in production you'd iterate by ID
  }
  _sendJson(req, 200, doc);
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

void WebServerManager::_handleSaveConfig(AsyncWebServerRequest* req) {
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
