#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "config.h"
#include "instrumentManager.h"
#include "actuatorDriver.h"
#include "loopEngine.h"
#include "midiEngine.h"
#include "storage.h"

// ============================================================================
// Web server with REST API + WebSocket for real-time updates
// ============================================================================

class WebServerManager {
public:
  WebServerManager(InstrumentManager* instrMgr, ActuatorDriver* driver,
                   LoopEngine* loopEngine, MidiEngine* midiEngine, Storage* storage);

  bool begin();
  void update();  // Call in loop() for WebSocket cleanup

private:
  AsyncWebServer _server;
  AsyncWebSocket _ws;

  InstrumentManager* _instrMgr;
  ActuatorDriver* _driver;
  LoopEngine* _loopEngine;
  MidiEngine* _midiEngine;
  Storage* _storage;

  // Route setup
  void _setupRoutes();

  // --- REST API handlers ---

  // Instruments CRUD
  void _handleGetInstruments(AsyncWebServerRequest* req);
  void _handleGetInstrument(AsyncWebServerRequest* req);
  void _handleCreateInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleUpdateInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleDeleteInstrument(AsyncWebServerRequest* req);

  // Actuators CRUD
  void _handleAddActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleUpdateActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleDeleteActuator(AsyncWebServerRequest* req);

  // Testing
  void _handleTestInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len);
  void _handleTestActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len);

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
  void _handleSaveConfig(AsyncWebServerRequest* req);

  // WebSocket
  void _onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                  AwsEventType type, void* arg, uint8_t* data, size_t len);

  // Helpers
  void _sendJson(AsyncWebServerRequest* req, int code, const JsonDocument& doc);
  void _sendError(AsyncWebServerRequest* req, int code, const char* msg);
  uint8_t _extractId(AsyncWebServerRequest* req, const char* param);

  // WebSocket broadcast
  void _wsBroadcast(const char* type, const JsonObject& data);
};

#endif // WEB_SERVER_H
