#include "web_server.h"
#include "../actuator/actuator_validation.h"
#include "../core/reconfig_barrier.h"

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
  // Hold the RT core parked for the WHOLE transaction (validate -> mutate ->
  // rebuild -> persist -> commit-or-rollback). The barrier is reentrant, so the
  // nested acquisition inside rebuildAll() succeeds immediately; and if the RT
  // core cannot be parked, we refuse before touching anything rather than
  // reconfiguring underneath it.
  ReconfigLock lock;
  if (!lock.ok()) { _sendError(req, 503, "Reconfiguration busy, no change applied"); return; }

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

  // addConfig() only compares against other ACTUATORS. Check the pins this
  // config wants against everything else in the running system too (LED strips,
  // a fitted microphone), which nothing did before.
  {
    const uint8_t pins[] = { cfg.hwPin, cfg.hwAddress, cfg.endStopPin1, cfg.endStopPin2 };
    for (uint8_t i = 0; i < 4; i++) {
      if (!actuatorConfigUsesGpio(cfg, pins[i])) continue;
      const char* owner = nullptr;
      if (_gpioClaimedBy(pins[i], 0xFF, owner) &&
          strcmp(owner ? owner : "", "an actuator") != 0) {
        char msg[112];
        snprintf(msg, sizeof(msg), "GPIO %u is already used by %s",
                 (unsigned)pins[i], owner);
        _sendError(req, 409, msg);
        return;
      }
    }
  }

  int idx = _actFactory->addConfig(cfg);
  if (idx < 0) { _sendError(req, 507, "Max actuators reached (or invalid config)"); return; }

  uint8_t newId = _actFactory->getConfig(idx).id;

  // Reconstruire les actionneurs physiques
  _actFactory->rebuildAll();
  // review #8: check persistence, roll back the new config on failure.
  if (!_storage->saveActuators(*_actFactory)) {
    _actFactory->removeConfig(newId);
    _actFactory->rebuildAll();
    _sendError(req, 500, "Failed to persist actuator");
    return;
  }

  JsonDocument resp;
  resp["id"] = newId;
  resp["message"] = "Actuator created";
  _sendJson(req, 201, resp);
}

void WebServerManager::_handleUpdateActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  ReconfigLock lock;   // see _handleCreateActuator
  if (!lock.ok()) { _sendError(req, 503, "Reconfiguration busy, no change applied"); return; }

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

  // The create path rejects a config that fights over a physical pin/channel;
  // the update path used to skip that check entirely, so editing an actuator
  // could silently move it onto a GPIO another actuator already drives.
  // `id` is excluded so a config never conflicts with its own previous state.
  uint8_t conflictId = 0;
  if (_actFactory->hasResourceConflict(candidate, id, conflictId)) {
    char msg[96];
    snprintf(msg, sizeof(msg),
             "Hardware resource conflict with actuator id %u (shared GPIO, MCP pin or PCA channel)",
             (unsigned)conflictId);
    _sendError(req, 409, msg);
    return;  // live config untouched
  }

  const ActuatorConfig previous = *cfg;   // for rollback if persistence fails
  *cfg = candidate;  // commit only after validation passes

  // Reconstruire les actionneurs physiques
  _actFactory->rebuildAll();
  if (!_storage->saveActuators(*_actFactory)) {   // review #8
    // Roll RAM back so it matches what is actually on flash — otherwise the
    // engine runs a config that a reboot would silently discard.
    *cfg = previous;
    _actFactory->rebuildAll();
    _sendError(req, 500, "Failed to persist actuator (change rolled back)");
    return;
  }

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
  ReconfigLock lock;   // see _handleCreateActuator
  if (!lock.ok()) { _sendError(req, 503, "Reconfiguration busy, no change applied"); return; }

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
  bool ok = _storage->saveActuators(*_actFactory);   // review #8

  if (depCount > 0 && force) {
    ok = _storage->saveInstruments(*_instrMgr) && ok;
    _recompileLookupFromInstruments();  // rebuild pipelines without the dead ID
  }

  if (!ok) {
    _sendError(req, 500, "Actuator removed in RAM but persistence failed");
    return;
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
