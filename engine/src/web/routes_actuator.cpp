#include "web_server.h"
#include "../actuator/actuator_validation.h"

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
    obj["endStopPin1"] = cfg.endStopPin1;
    obj["endStopPin2"] = cfg.endStopPin2;

    // Etat temps reel
    Actuator* act = _actMgr->getActuator(cfg.id);
    obj["active"] = act ? act->isActive() : false;
  }
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleGetActuatorStatus(AsyncWebServerRequest* req) {
  // Lightweight endpoint for polling: returns only id + active state
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < _actFactory->getConfigCount(); i++) {
    const ActuatorConfig& cfg = _actFactory->getConfig(i);
    Actuator* act = _actMgr->getActuator(cfg.id);
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = cfg.id;
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
  cfg.endStopPin1 = doc["endStopPin1"] | 0xFF;
  cfg.endStopPin2 = doc["endStopPin2"] | 0xFF;

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

  // P1 #13: apply the patch to a CANDIDATE copy and validate it before
  // committing. Previously the live config in RAM was mutated field-by-field
  // and, on validation failure, left partially modified.
  ActuatorConfig candidate = *cfg;
  if (doc.containsKey("name"))        strlcpy(candidate.name, doc["name"] | "Actuator", sizeof(candidate.name));
  if (doc.containsKey("type"))        candidate.type = (ActuatorType)(doc["type"] | 0);
  if (doc.containsKey("behavior"))    candidate.behavior = (ActuatorBehavior)(doc["behavior"] | 0);
  if (doc.containsKey("bus"))         candidate.bus = (HardwareBus)(doc["bus"] | 0);
  if (doc.containsKey("hwAddress"))   candidate.hwAddress = doc["hwAddress"] | 0x20;
  if (doc.containsKey("hwPin"))       candidate.hwPin = doc["hwPin"] | 0;
  if (doc.containsKey("paramMin"))    candidate.paramMin = doc["paramMin"] | 8;
  if (doc.containsKey("paramMax"))    candidate.paramMax = doc["paramMax"] | 30;
  if (doc.containsKey("paramDefault")) candidate.paramDefault = doc["paramDefault"] | 15;
  if (doc.containsKey("cooldownUs"))  candidate.cooldownUs = doc["cooldownUs"] | 200;
  if (doc.containsKey("enabled"))     candidate.enabled = doc["enabled"] | true;
  if (doc.containsKey("inverted"))    candidate.inverted = doc["inverted"] | false;
  if (doc.containsKey("endStopPin1")) candidate.endStopPin1 = doc["endStopPin1"] | 0xFF;
  if (doc.containsKey("endStopPin2")) candidate.endStopPin2 = doc["endStopPin2"] | 0xFF;

  const char* errMsg = nullptr;
  if (!_validateActuatorConfig(candidate, errMsg)) {
    _sendError(req, 400, errMsg);
    return;  // live config untouched
  }

  *cfg = candidate;  // commit only after validation passes

  // Reconstruire les actionneurs physiques
  _actFactory->rebuildAll();
  _storage->saveActuators(*_actFactory);

  _sendError(req, 200, "Updated");
}

// Does instrument `inst` reference actuator `actId` (in its actuator list, its
// note actions or its CC bindings)?
static bool instrumentReferencesActuator(const InstrumentConfig& inst, uint8_t actId) {
  for (uint8_t i = 0; i < inst.actuatorCount; i++)
    if (inst.actuatorIds[i] == actId) return true;
  for (uint8_t i = 0; i < inst.noteOnCount; i++)
    if (inst.noteOnActions[i].actuator_id == actId) return true;
  for (uint8_t i = 0; i < inst.noteOffCount; i++)
    if (inst.noteOffActions[i].actuator_id == actId) return true;
  for (uint8_t i = 0; i < inst.ccBindingCount; i++)
    if (inst.ccBindings[i].actuator_id == actId) return true;
  return false;
}

// Strip every reference to actuator `actId` out of `inst` (force delete).
static void stripActuatorReferences(InstrumentConfig& inst, uint8_t actId) {
  uint8_t w = 0;
  for (uint8_t r = 0; r < inst.actuatorCount; r++)
    if (inst.actuatorIds[r] != actId) inst.actuatorIds[w++] = inst.actuatorIds[r];
  for (uint8_t i = w; i < inst.actuatorCount; i++) inst.actuatorIds[i] = 0xFF;
  inst.actuatorCount = w;

  w = 0;
  for (uint8_t r = 0; r < inst.noteOnCount; r++)
    if (inst.noteOnActions[r].actuator_id != actId) inst.noteOnActions[w++] = inst.noteOnActions[r];
  inst.noteOnCount = w;

  w = 0;
  for (uint8_t r = 0; r < inst.noteOffCount; r++)
    if (inst.noteOffActions[r].actuator_id != actId) inst.noteOffActions[w++] = inst.noteOffActions[r];
  inst.noteOffCount = w;

  w = 0;
  for (uint8_t r = 0; r < inst.ccBindingCount; r++)
    if (inst.ccBindings[r].actuator_id != actId) inst.ccBindings[w++] = inst.ccBindings[r];
  inst.ccBindingCount = w;
}

void WebServerManager::_handleDeleteActuator(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  if (!_actFactory->findConfig(id)) {
    _sendError(req, 404, "Actuator config not found"); return;
  }

  bool force = false;
  if (req->hasParam("force")) {
    String f = req->getParam("force")->value();
    force = (f == "true" || f == "1");
  }

  // P1 #14: refuse to orphan references unless the caller explicitly forces it.
  uint8_t depCount = 0;
  for (uint8_t i = 0; i < _instrMgr->getInstrumentCount(); i++) {
    InstrumentConfig* inst = _instrMgr->getInstrumentByIndex(i);
    if (inst && instrumentReferencesActuator(*inst, id)) depCount++;
  }

  if (depCount > 0 && !force) {
    JsonDocument doc;
    doc["message"] = "Actuator is still referenced by instruments; delete them "
                     "first or retry with ?force=true";
    JsonArray deps = doc["dependencies"].to<JsonArray>();
    for (uint8_t i = 0; i < _instrMgr->getInstrumentCount(); i++) {
      InstrumentConfig* inst = _instrMgr->getInstrumentByIndex(i);
      if (inst && instrumentReferencesActuator(*inst, id)) {
        JsonObject d = deps.add<JsonObject>();
        d["id"] = inst->id;
        d["name"] = inst->name;
      }
    }
    _sendJson(req, 409, doc);
    return;
  }

  // Forced delete: clean out every reference so nothing points at a dead ID.
  if (depCount > 0 && force) {
    for (uint8_t i = 0; i < _instrMgr->getInstrumentCount(); i++) {
      InstrumentConfig* inst = _instrMgr->getInstrumentByIndex(i);
      if (inst) stripActuatorReferences(*inst, id);
    }
  }

  _actFactory->removeConfig(id);
  _actFactory->rebuildAll();
  _storage->saveActuators(*_actFactory);

  if (depCount > 0 && force) {
    _storage->saveInstruments(*_instrMgr);
    _recompileLookupFromInstruments();  // rebuild pipelines without the dead ID
  }

  _sendError(req, 200, "Deleted");
}

void WebServerManager::_handleTestActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }
  uint8_t id = doc["id"] | 0;

  // Raw angle mode: bypass paramMin/paramMax for servo calibration
  if (doc.containsKey("angle")) {
    uint8_t angle = doc["angle"] | 90;
    _actMgr->testServoAngle(id, angle);
    _sendError(req, 200, "Servo angle test");
    return;
  }

  uint8_t val = doc["value"] | 80;
  _actMgr->testActuator(id, val);
  _sendError(req, 200, "Testing");
}

bool WebServerManager::_validateActuatorConfig(const ActuatorConfig& cfg, const char*& errMsg) {
  // P1 #15: delegate to the shared validator (also enforced in
  // ActuatorFactory::addConfig, so LittleFS load / migration / templates are
  // covered by the same rules).
  return validateActuatorConfig(cfg, errMsg);
}
