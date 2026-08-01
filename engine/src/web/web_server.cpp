#include "web_server.h"
#include "index_html_gz.h"
#include "../core/panic_state.h"

// P0 #3: reject actuator-driving requests with 409 while the panic latch is set
// (the RT-side guards already refuse the actuation; this gives the client clear
// feedback instead of a misleading 200).
bool WebServerManager::_panicActiveReject(AsyncWebServerRequest* req) {
  if (g_panicActive.load(std::memory_order_acquire)) {
    _sendError(req, 409, "Panic active — POST /api/panic/reset to re-arm");
    return true;
  }
  return false;
}

WebServerManager::WebServerManager(InstrumentManager* instrMgr, ActuatorManager* actMgr,
                                    ActuatorFactory* actFactory,
                                    LoopEngine* loopEngine, MidiEngine* midiEngine,
                                    Storage* storage, Scheduler* scheduler,
                                    EventProcessor* eventProc, LedEngine* ledEngine)
  : _server(WEB_PORT), _ws(WS_PATH),
    _instrMgr(instrMgr), _actMgr(actMgr), _actFactory(actFactory),
    _loopEngine(loopEngine),
    _midiEngine(midiEngine), _storage(storage), _scheduler(scheduler),
    _eventProc(eventProc), _ledEngine(ledEngine) {}

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
  // Broadcast MIDI note changes via WebSocket (~every 30ms)
  if (_ws.count() > 0) {
    uint32_t now = micros();
    if (now - _wsLastBroadcastUs >= 30000) {
      _wsLastBroadcastUs = now;
      _broadcastNoteChanges();
    }
  }
}

void WebServerManager::_broadcastNoteChanges() {
  if (!_eventProc) return;
  uint8_t current[128];
  _eventProc->getNoteActive(current);

  // Fixed stack buffer — avoids String heap fragmentation that causes
  // truncated JSON ("unexpected end of JSON input" on client).
  // Max worst case: 128 notes * ~30 chars each ≈ 3840 + overhead < 4096
  char buf[4096];
  uint16_t pos = 0;
  uint8_t changeCount = 0;

  for (uint8_t i = 0; i < 128; i++) {
    bool wasActive = _wsNoteCache[i] > 0;
    bool isActive = current[i] > 0;
    if (wasActive != isActive) {
      if (changeCount == 0) {
        // Start the JSON array
        pos = snprintf(buf, sizeof(buf), "{\"type\":\"midi_notes\",\"notes\":[");
      } else {
        if (pos < sizeof(buf) - 1) buf[pos++] = ',';
      }
      pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"n\":%d,\"on\":%s,\"v\":%d}",
                       i, isActive ? "true" : "false", current[i]);
      changeCount++;
    }
  }

  if (changeCount == 0) return;

  // Close JSON array
  if (pos < sizeof(buf) - 2) {
    buf[pos++] = ']';
    buf[pos++] = '}';
    buf[pos] = '\0';
  }

  _ws.textAll(buf);
  memcpy(_wsNoteCache, current, 128);
}

void WebServerManager::_setupRoutes() {
  // UI web: serve from LittleFS if available, otherwise from embedded PROGMEM gzip
  // Cache-Control: no-cache forces revalidation on every load (fixes phone browser caching)
  // SECURITY (P0 #1): Serve ONLY the single-page UI. Do NOT expose the whole
  // LittleFS root — that would allow fetching sensitive files directly
  // (/auth.json with the API token + PIN, /wifi.json with the plaintext WiFi
  // password, /actuators.json, /instruments.json, /error.log, /loops/*, ...).
  // The UI is a self-contained index.html (inline CSS/JS), so a single
  // whitelisted route is all that is required. Any other path falls through
  // to the onNotFound handler (404). serveStatic("/", LittleFS, "/") is
  // deliberately NOT registered.
  auto serveIndex = [](AsyncWebServerRequest* req) {
    if (LittleFS.exists("/index.html")) {
      AsyncWebServerResponse* response = req->beginResponse(LittleFS, "/index.html", "text/html");
      response->addHeader("Cache-Control", "no-cache, must-revalidate");
      req->send(response);
    } else {
      // Modern overload (works on esp32async 3.x; beginResponse_P is deprecated).
      // PROGMEM on ESP32 is memory-mapped, so the plain buffer overload is fine.
      AsyncWebServerResponse* response = req->beginResponse(
        200, "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "no-cache, must-revalidate");
      req->send(response);
    }
  };
  _server.on("/", HTTP_GET, serveIndex);
  _server.on("/index.html", HTTP_GET, serveIndex);

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

  _server.on("/api/actuators/status", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetActuatorStatus(req); });

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
        if (_panicActiveReject(req)) return;
        _handleTestInstrument(req, data, len);
      }
    });

  _server.on("/api/instruments/test-action", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        if (_panicActiveReject(req)) return;
        _handleTestInstrumentAction(req, data, len);
      }
    });

  _server.on("/api/test/actuator", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        if (_panicActiveReject(req)) return;
        _handleTestActuator(req, data, len);
      }
    });

  _server.on("/api/test/cc", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        if (_panicActiveReject(req)) return;
        _handleTestCC(req, data, len);
      }
    });

  _server.on("/api/test/note", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        if (_panicActiveReject(req)) return;
        _handleTestNote(req, data, len);
      }
    });

  // Active notes (for real-time piano display)
  _server.on("/api/notes/active", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetActiveNotes(req); });

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

  _server.on("/api/loop/pause", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _loopEngine->pause();
      JsonDocument doc;
      doc["message"] = "Paused";
      doc["paused"] = _loopEngine->isPaused();
      _sendJson(req, 200, doc);
    });

  _server.on("/api/loop/resume", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _loopEngine->resume();
      JsonDocument doc;
      doc["message"] = "Resumed";
      doc["paused"] = _loopEngine->isPaused();
      _sendJson(req, 200, doc);
    });

  _server.on("/api/loop/chain", HTTP_POST,
    [this](AsyncWebServerRequest* req) {},
    NULL,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handlePlayChain(req, data, len);
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

  // --- Auth & Login ---
  _server.on("/api/auth-token", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetAuthToken(req); });

  // H7: POST route for PIN-based token retrieval (PIN in body, not query string)
  _server.on("/api/auth-token", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handlePostAuthToken(req, data, len);
    });

  _server.on("/api/login-status", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleLoginStatus(req); });

  // review #25: a real session-validation endpoint. GET routes skip auth, so the
  // UI could not actually verify a saved token (any string "passed"). This POST
  // requires the Bearer token via checkAuth: 200 => valid, 401 => invalid.
  _server.on("/api/auth/check", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;   // sends 401 on failure
      JsonDocument doc;
      doc["ok"] = true;
      _sendJson(req, 200, doc);
    });

  _server.on("/api/pin", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleSetPin(req, data, len);
      }
    });

  // --- Validation ---
  _server.on("/api/validate", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleValidateConfig(req); });

  // --- Factory Reset ---
  _server.on("/api/factory-reset", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handleFactoryReset(req);
    });

  // --- Emergency Stop / Panic (P0 #3) ---
  // Real "STOP ALL": bypasses the normal command queue and drives every
  // actuator OFF directly via ActuatorManager::stopAll(). value=0 test
  // commands are NOT a reliable OFF (a solenoid maps 0 to a min-duration
  // pulse, a servo to its min position, a motor to remapped PWM), so the UI
  // must call this endpoint instead.
  _server.on("/api/panic", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handlePanic(req);
    });

  // Re-arm after an emergency stop (clears the panic latch). Explicit only.
  _server.on("/api/panic/reset", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handlePanicReset(req);
    });

  // --- MIDI Channel Config ---
  _server.on("/api/midi/channels", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetMidiChannels(req); });

  _server.on("/api/midi/channels", HTTP_PUT,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleSetMidiChannels(req, data, len);
      }
    });

  // --- LED Strips API ---
  _server.on("/api/led/strips", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetLedStrips(req); });

  _server.on("/api/led/strips", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleConfigureLedStrip(req, data, len);
      }
    });

  _server.on("/api/led/strip", HTTP_DELETE,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handleDeleteLedStrip(req);
    });

  _server.on("/api/led/strip/test", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleTestLedStrip(req, data, len);
      }
    });

  // --- LED Segments API ---
  _server.on("/api/led/segments", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetLedSegments(req); });

  _server.on("/api/led/segments", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleCreateLedSegment(req, data, len);
      }
    });

  _server.on("/api/led/segment", HTTP_PUT,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleUpdateLedSegment(req, data, len);
      }
    });

  _server.on("/api/led/segment", HTTP_DELETE,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handleDeleteLedSegment(req);
    });

  _server.on("/api/led/segment/test", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleTestLedSegment(req, data, len);
      }
    });

  // --- LED Themes API ---
  _server.on("/api/led/themes", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetLedThemes(req); });

  _server.on("/api/led/themes", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleCreateLedTheme(req, data, len);
      }
    });

  _server.on("/api/led/theme", HTTP_PUT,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleUpdateLedTheme(req, data, len);
      }
    });

  _server.on("/api/led/theme", HTTP_DELETE,
    [this](AsyncWebServerRequest* req) {
      if (!_rateLimiter.checkRate(req)) return;
      if (!_auth.checkAuth(req)) return;
      _handleDeleteLedTheme(req);
    });

  _server.on("/api/led/theme/active", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleSetActiveLedTheme(req, data, len);
      }
    });

  // --- Pipeline Editor API ---
  _server.on("/api/pipeline/block-schema", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetPipelineBlockSchema(req); });

  _server.on("/api/pipeline/blocks", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetPipelineBlocks(req); });

  _server.on("/api/pipeline/blocks", HTTP_PUT,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleSetPipelineBlocks(req, data, len);
      }
    });

  _server.on("/api/pipeline/test", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        if (_panicActiveReject(req)) return;
        _handleTestPipeline(req, data, len);
      }
    });

  // --- LED Status / Brightness ---
  _server.on("/api/led/status", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetLedStatus(req); });

  _server.on("/api/led/brightness", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleSetLedBrightness(req, data, len);
      }
    });

  // --- Microphone / Latency Measurement ---
  _server.on("/api/mic/status", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetMicStatus(req); });

  _server.on("/api/mic/measure-latency", HTTP_POST,
    [this](AsyncWebServerRequest* req) { if (!_rateLimiter.checkRate(req)) return; },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        if (!_auth.checkAuth(req)) return;
        _handleMeasureLatency(req, data, len);
      }
    });

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
        // SECURITY (P0 #17): do NOT write data[len]=0 — the ESPAsyncWebServer
        // frame buffer is not guaranteed to have a spare byte past `len`, so
        // that is an out-of-bounds write. Parse with the explicit length.
        JsonDocument doc;
        if (!deserializeJson(doc, data, len)) {
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
  // Use stack buffer to avoid heap fragmentation from String allocations
  char buf[512];
  size_t len = serializeJson(doc, buf, sizeof(buf));
  if (len > 0 && len < sizeof(buf)) {
    _ws.textAll(buf, len);
  }
}

// --- Helpers ---

void WebServerManager::_sendJson(AsyncWebServerRequest* req, int code, const JsonDocument& doc) {
  // Pre-allocate String with known size to avoid reallocation fragmentation
  size_t len = measureJson(doc);
  String json;
  json.reserve(len + 1);
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
    int val = req->getParam(param)->value().toInt();
    if (val < 0 || val > 254) return 0xFF;  // 0xFF = invalid sentinel
    return (uint8_t)val;
  }
  return 0xFF;  // 0xFF = missing param sentinel (0 is a valid ID)
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
