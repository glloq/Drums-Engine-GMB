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

// ============================================================================
// WebServerManager - REST API + WebSocket
// ============================================================================

class WebServerManager {
public:
  WebServerManager(InstrumentManager* instrMgr, ActuatorManager* actMgr,
                   ActuatorFactory* actFactory,
                   LoopEngine* loopEngine, MidiEngine* midiEngine,
                   Storage* storage, Scheduler* scheduler,
                   EventProcessor* eventProc);

  bool begin();
  void update();

private:
  AsyncWebServer _server;
  AsyncWebSocket _ws;

  InstrumentManager* _instrMgr;
  ActuatorManager* _actMgr;
  ActuatorFactory* _actFactory;
  LoopEngine* _loopEngine;
  MidiEngine* _midiEngine;
  Storage* _storage;
  Scheduler* _scheduler;
  EventProcessor* _eventProc;

  void _setupRoutes();

  // Instruments CRUD
  void _handleGetInstruments(AsyncWebServerRequest* req);
  void _handleGetInstrument(AsyncWebServerRequest* req);
  void _handleCreateInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleUpdateInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleDeleteInstrument(AsyncWebServerRequest* req);

  // Actuators CRUD
  void _handleGetActuators(AsyncWebServerRequest* req);
  void _handleCreateActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleUpdateActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleDeleteActuator(AsyncWebServerRequest* req);
  void _handleTestActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len);

  // Testing
  void _handleTestInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleTestInstrumentAction(AsyncWebServerRequest* req, uint8_t* data, size_t len);

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

  // WebSocket
  void _onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                  AwsEventType type, void* arg, uint8_t* data, size_t len);

  // Helpers
  void _sendJson(AsyncWebServerRequest* req, int code, const JsonDocument& doc);
  void _sendError(AsyncWebServerRequest* req, int code, const char* msg);
  uint8_t _extractId(AsyncWebServerRequest* req, const char* param);
  void _wsBroadcast(const char* type, const JsonObject& data);

  bool _validateActuatorConfig(const ActuatorConfig& cfg, const char*& errMsg);
  bool _validateInstrumentConfig(InstrumentConfig& cfg, const char*& errMsg);
  void _recompileLookupFromInstruments();
};

#endif // WEB_SERVER_H
