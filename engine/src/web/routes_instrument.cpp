#include "web_server.h"
#include "../instrument/instrument_validation.h"

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

// --- Testing ---

void WebServerManager::_handleTestInstrument(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }
  uint8_t id = doc["id"] | 0;
  uint8_t vel = doc["velocity"] | 80;
  _instrMgr->testInstrument(id, vel);
  _sendError(req, 200, "Testing");
}

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

bool WebServerManager::_validateInstrumentConfig(InstrumentConfig& cfg, const char*& errMsg) {
  // review #14: delegate to the shared validator (also used by the LittleFS
  // load path). The lookup resolves actuator ids against the factory pool.
  auto lookup = [](void* ctx, uint8_t id) -> const ActuatorConfig* {
    return ((ActuatorFactory*)ctx)->findConfig(id);
  };
  return validateInstrumentConfig(cfg, errMsg, lookup, _actFactory);
}
