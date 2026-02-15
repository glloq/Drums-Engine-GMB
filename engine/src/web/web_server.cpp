#include "web_server.h"
#include "index_html_gz.h"

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
  // Initialize authentication (load or generate API token)
  _auth.begin();

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
  // UI web: serve from LittleFS if available, otherwise from embedded PROGMEM gzip
  _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (LittleFS.exists("/index.html")) {
      req->send(LittleFS, "/index.html", "text/html");
    } else {
      AsyncWebServerResponse* response = req->beginResponse_P(
        200, "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
      response->addHeader("Content-Encoding", "gzip");
      req->send(response);
    }
  });
  _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // --- Instruments API ---
  _server.on("/api/instruments", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetInstruments(req); });

  _server.on("/api/instrument", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetInstrument(req); });

  _server.on("/api/instruments", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleCreateInstrument(req, data, len);
      }
    });

  _server.on("/api/instrument", HTTP_PUT,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleUpdateInstrument(req, data, len);
      }
    });

  _server.on("/api/instrument", HTTP_DELETE,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handleDeleteInstrument(req);
    });

  // --- Actuators API ---
  _server.on("/api/actuators", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetActuators(req); });

  _server.on("/api/actuators", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleCreateActuator(req, data, len);
      }
    });

  _server.on("/api/actuator", HTTP_PUT,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleUpdateActuator(req, data, len);
      }
    });

  _server.on("/api/actuator", HTTP_DELETE,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handleDeleteActuator(req);
    });

  // --- Test API ---
  _server.on("/api/test/instrument", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleTestInstrument(req, data, len);
      }
    });

  _server.on("/api/instruments/test-action", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleTestInstrumentAction(req, data, len);
      }
    });

  _server.on("/api/test/actuator", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleTestActuator(req, data, len);
      }
    });

  // --- Loops API ---
  _server.on("/api/loops", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetLoops(req); });

  _server.on("/api/loop", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetLoop(req); });

  _server.on("/api/loops", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleCreateLoop(req, data, len);
      }
    });

  _server.on("/api/loop", HTTP_PUT,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleUpdateLoop(req, data, len);
      }
    });

  _server.on("/api/loop", HTTP_DELETE,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handleDeleteLoop(req);
    });

  _server.on("/api/loop/play", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handlePlayLoop(req);
    });

  _server.on("/api/loop/stop", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handleStopLoop(req);
    });

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
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleCreateInstrumentFromTemplate(req, data, len);
      }
    });

  _server.on("/api/save", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handleSaveConfig(req);
    });

  // --- Logs API ---
  _server.on("/api/logs", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetLogs(req); });

  _server.on("/api/logs", HTTP_DELETE,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handleClearLogs(req);
    });

  // --- Auth token API (local access only) ---
  _server.on("/api/auth-token", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetAuthToken(req); });

  _server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "application/json", "{\"error\":\"Not found\"}");
  });
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

void WebServerManager::_recompileLookupFromInstruments() {
  if (_recompileCb) {
    // Use external callback (compilePipelines from main.cpp) which handles
    // the full instrument->pipeline recompilation with actuator resolution
    _recompileCb();
  } else {
    // Fallback: recompile from JSON representation
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    _instrMgr->toJson(arr);
    PipelineCompiler::compileAll(arr, _eventProc->getLookup());
  }
}
