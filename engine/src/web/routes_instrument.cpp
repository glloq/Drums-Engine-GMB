#include "web_server.h"

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
  if (!_parseJsonBody(req, data, len, doc)) return;

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
  if (!_parseJsonBody(req, data, len, doc)) return;

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
  if (!_parseJsonBody(req, data, len, doc)) return;
  uint8_t id = doc["id"] | 0;
  uint8_t vel = doc["velocity"] | 80;
  _instrMgr->testInstrument(id, vel);
  _sendError(req, 200, "Testing");
}

void WebServerManager::_handleTestInstrumentAction(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (!_parseJsonBody(req, data, len, doc)) return;

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
        // PULSE: solenoid (strike/hold) + servo (strike, hi-hat splash)
        return actCfg.type == ActuatorType::SOLENOID || actCfg.type == ActuatorType::SERVO;
      case CommandType::POSITION:
        return actCfg.type == ActuatorType::SERVO || actCfg.type == ActuatorType::MOTOR_OPTICAL;
      case CommandType::PWM:
        // PWM: motors + servos (comb/brush oscillation)
        return actCfg.type == ActuatorType::PWM_MOTOR || actCfg.type == ActuatorType::MOTOR_OPTICAL
            || actCfg.type == ActuatorType::SERVO;
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
