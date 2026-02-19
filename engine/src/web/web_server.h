#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "../core/config.h"
#include "../core/types.h"
#include "../instrument/instrument_manager.h"
#include "../actuator/actuator_manager.h"
#include "../actuator/actuator_factory.h"
#include "../loop/loop_engine.h"
#include "../midi/midi_engine.h"
#include "../storage/storage.h"
#include "../scheduler/scheduler.h"
#include "../event/event_processor.h"
#include "../event/pipeline_compiler.h"
#include "../led/led_engine.h"
#include "api_auth.h"

// ============================================================================
// WebServerManager - REST API + WebSocket + Auth + Rate-Limiting
// ============================================================================

class WebServerManager {
public:
  WebServerManager(InstrumentManager* instrMgr, ActuatorManager* actMgr,
                   ActuatorFactory* actFactory,
                   LoopEngine* loopEngine, MidiEngine* midiEngine,
                   Storage* storage, Scheduler* scheduler,
                   EventProcessor* eventProc, LedEngine* ledEngine);

  bool begin();
  void update();

  // Set callback for pipeline recompilation after instrument mutations
  void setRecompileCallback(void (*cb)()) { _recompileCb = cb; }

  // Expose server for external route registration (WiFiManager, etc.)
  AsyncWebServer& getServer() { return _server; }

  // Get API token for display at boot
  String getApiToken() const { return _auth.getToken(); }

private:
  AsyncWebServer _server;
  AsyncWebSocket _ws;
  ApiAuth _auth;
  RateLimiter _rateLimiter;
  void (*_recompileCb)() = nullptr;

  InstrumentManager* _instrMgr;
  ActuatorManager* _actMgr;
  ActuatorFactory* _actFactory;
  LoopEngine* _loopEngine;
  MidiEngine* _midiEngine;
  Storage* _storage;
  Scheduler* _scheduler;
  EventProcessor* _eventProc;
  LedEngine* _ledEngine;

  void _setupRoutes();

  // Instruments CRUD
  void _handleGetInstruments(AsyncWebServerRequest* req);
  void _handleGetInstrument(AsyncWebServerRequest* req);
  void _handleCreateInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleUpdateInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleDeleteInstrument(AsyncWebServerRequest* req);

  // Actuators CRUD
  void _handleGetActuators(AsyncWebServerRequest* req);
  void _handleGetActuatorStatus(AsyncWebServerRequest* req);
  void _handleCreateActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleUpdateActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleDeleteActuator(AsyncWebServerRequest* req);
  void _handleTestActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len);

  // Testing
  void _handleTestInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleTestInstrumentAction(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleTestCC(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleTestNote(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleGetActiveNotes(AsyncWebServerRequest* req);

  // Loops
  void _handleGetLoops(AsyncWebServerRequest* req);
  void _handleGetLoop(AsyncWebServerRequest* req);
  void _handleCreateLoop(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleUpdateLoop(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleDeleteLoop(AsyncWebServerRequest* req);
  void _handlePlayLoop(AsyncWebServerRequest* req);
  void _handleStopLoop(AsyncWebServerRequest* req);

  // System
  void _handleGetStatus(AsyncWebServerRequest* req);
  void _handleScanI2C(AsyncWebServerRequest* req);
  void _handleGetMidiNotes(AsyncWebServerRequest* req);
  void _handleGetCapabilities(AsyncWebServerRequest* req);
  void _handleGetCcRoutes(AsyncWebServerRequest* req);
  void _handleGetTemplates(AsyncWebServerRequest* req);
  void _handleCreateInstrumentFromTemplate(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleSaveConfig(AsyncWebServerRequest* req);
  void _handleGetLogs(AsyncWebServerRequest* req);
  void _handleClearLogs(AsyncWebServerRequest* req);
  void _handleGetAuthToken(AsyncWebServerRequest* req);
  void _handleGetMidiChannels(AsyncWebServerRequest* req);
  void _handleSetMidiChannels(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleFactoryReset(AsyncWebServerRequest* req);

  // LED Strips
  void _handleGetLedStrips(AsyncWebServerRequest* req);
  void _handleConfigureLedStrip(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleDeleteLedStrip(AsyncWebServerRequest* req);
  void _handleTestLedStrip(AsyncWebServerRequest* req, uint8_t* data, size_t len);

  // LED Segments
  void _handleGetLedSegments(AsyncWebServerRequest* req);
  void _handleCreateLedSegment(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleUpdateLedSegment(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleDeleteLedSegment(AsyncWebServerRequest* req);
  void _handleTestLedSegment(AsyncWebServerRequest* req, uint8_t* data, size_t len);

  // LED Themes
  void _handleGetLedThemes(AsyncWebServerRequest* req);
  void _handleCreateLedTheme(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleUpdateLedTheme(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleDeleteLedTheme(AsyncWebServerRequest* req);
  void _handleSetActiveLedTheme(AsyncWebServerRequest* req, uint8_t* data, size_t len);

  // LED Master
  void _handleGetLedStatus(AsyncWebServerRequest* req);
  void _handleSetLedBrightness(AsyncWebServerRequest* req, uint8_t* data, size_t len);

  // Pipeline Editor
  void _handleGetPipelineBlocks(AsyncWebServerRequest* req);
  void _handleGetPipelineBlockSchema(AsyncWebServerRequest* req);
  void _handleSetPipelineBlocks(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleTestPipeline(AsyncWebServerRequest* req, uint8_t* data, size_t len);

  // WebSocket
  void _onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                  AwsEventType type, void* arg, uint8_t* data, size_t len);

  // Helpers
  void _sendJson(AsyncWebServerRequest* req, int code, const JsonDocument& doc);
  void _sendError(AsyncWebServerRequest* req, int code, const char* msg);
  uint8_t _extractId(AsyncWebServerRequest* req, const char* param);
  void _wsBroadcast(const char* type, const JsonObject& data);

  // WebSocket MIDI note activity broadcast
  uint8_t _wsNoteCache[128] = {};
  uint32_t _wsLastBroadcastUs = 0;
  void _broadcastNoteChanges();

  bool _validateActuatorConfig(const ActuatorConfig& cfg, const char*& errMsg);
  bool _validateInstrumentConfig(InstrumentConfig& cfg, const char*& errMsg);
  void _recompileLookupFromInstruments();
};

#endif // WEB_SERVER_H
