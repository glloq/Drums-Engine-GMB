#include "web_server.h"

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

  if (doc.containsKey("name"))        strlcpy(cfg->name, doc["name"] | "Actuator", sizeof(cfg->name));
  if (doc.containsKey("type"))        cfg->type = (ActuatorType)(doc["type"] | 0);
  if (doc.containsKey("behavior"))    cfg->behavior = (ActuatorBehavior)(doc["behavior"] | 0);
  if (doc.containsKey("bus"))         cfg->bus = (HardwareBus)(doc["bus"] | 0);
  if (doc.containsKey("hwAddress"))   cfg->hwAddress = doc["hwAddress"] | 0x20;
  if (doc.containsKey("hwPin"))       cfg->hwPin = doc["hwPin"] | 0;
  if (doc.containsKey("paramMin"))    cfg->paramMin = doc["paramMin"] | 8;
  if (doc.containsKey("paramMax"))    cfg->paramMax = doc["paramMax"] | 30;
  if (doc.containsKey("paramDefault")) cfg->paramDefault = doc["paramDefault"] | 15;
  if (doc.containsKey("cooldownUs"))  cfg->cooldownUs = doc["cooldownUs"] | 200;
  if (doc.containsKey("enabled"))     cfg->enabled = doc["enabled"] | true;
  if (doc.containsKey("inverted"))    cfg->inverted = doc["inverted"] | false;
  if (doc.containsKey("endStopPin1")) cfg->endStopPin1 = doc["endStopPin1"] | 0xFF;
  if (doc.containsKey("endStopPin2")) cfg->endStopPin2 = doc["endStopPin2"] | 0xFF;

  const char* errMsg = nullptr;
  if (!_validateActuatorConfig(*cfg, errMsg)) {
    _sendError(req, 400, errMsg);
    return;
  }

  // Reconstruire les actionneurs physiques
  _actFactory->rebuildAll();
  _storage->saveActuators(*_actFactory);

  _sendError(req, 200, "Updated");
}

void WebServerManager::_handleDeleteActuator(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  if (!_actFactory->removeConfig(id)) {
    _sendError(req, 404, "Actuator config not found"); return;
  }

  _actFactory->rebuildAll();
  _storage->saveActuators(*_actFactory);
  _sendError(req, 200, "Deleted");
}

void WebServerManager::_handleTestActuator(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }
  uint8_t id = doc["id"] | 0;
  uint8_t val = doc["value"] | 80;
  _actMgr->testActuator(id, val);
  _sendError(req, 200, "Testing");
}

bool WebServerManager::_validateActuatorConfig(const ActuatorConfig& cfg, const char*& errMsg) {
  // SOLENOID_HOLD: paramMin=pwmActivation, paramMax=pwmHold (paramMin CAN be > paramMax)
  // Other behaviors: paramMin must be <= paramMax
  if (cfg.behavior != ActuatorBehavior::SOLENOID_HOLD) {
    if (cfg.paramMin > cfg.paramMax) {
      errMsg = "paramMin must be <= paramMax";
      return false;
    }
    if (cfg.paramDefault < cfg.paramMin || cfg.paramDefault > cfg.paramMax) {
      errMsg = "paramDefault must be inside [paramMin, paramMax]";
      return false;
    }
  }

  if (cfg.type == ActuatorType::SERVO) {
    if (cfg.bus != HardwareBus::PCA9685) {
      errMsg = "Servo currently requires PCA9685 bus";
      return false;
    }
    if (cfg.hwPin > 15) {
      errMsg = "Servo channel must be in [0..15]";
      return false;
    }
  }

  if (cfg.type == ActuatorType::SOLENOID) {
    if (cfg.bus != HardwareBus::MCP23017 && cfg.bus != HardwareBus::GPIO_DIRECT) {
      errMsg = "Solenoid supports MCP23017 or GPIO_DIRECT";
      return false;
    }
  }

  if (cfg.type == ActuatorType::PWM_MOTOR) {
    if (cfg.bus != HardwareBus::LEDC_PWM && cfg.bus != HardwareBus::PCA9685) {
      errMsg = "PWM motor supports LEDC_PWM or PCA9685";
      return false;
    }
    // Valider le behavior pour PWM_MOTOR
    if (cfg.behavior != ActuatorBehavior::MOTOR_TIMED &&
        cfg.behavior != ActuatorBehavior::MOTOR_SPEED &&
        cfg.behavior != ActuatorBehavior::MOTOR_SWEEP &&
        cfg.behavior != ActuatorBehavior::MOTOR_ALTERNATE) {
      errMsg = "PWM motor behavior must be MOTOR_TIMED, MOTOR_SPEED, MOTOR_SWEEP or MOTOR_ALTERNATE";
      return false;
    }
    // MOTOR_SWEEP requiert au moins un end stop
    if (cfg.behavior == ActuatorBehavior::MOTOR_SWEEP) {
      if (cfg.endStopPin1 == 0xFF && cfg.endStopPin2 == 0xFF) {
        errMsg = "MOTOR_SWEEP requires at least one endStopPin";
        return false;
      }
    }
    // MOTOR_ALTERNATE requiert end stop + direction pin (hwAddress)
    if (cfg.behavior == ActuatorBehavior::MOTOR_ALTERNATE) {
      if (cfg.endStopPin1 == 0xFF && cfg.endStopPin2 == 0xFF) {
        errMsg = "MOTOR_ALTERNATE requires at least one endStopPin";
        return false;
      }
      if (cfg.bus != HardwareBus::LEDC_PWM) {
        errMsg = "MOTOR_ALTERNATE requires LEDC_PWM bus (direction pin via hwAddress)";
        return false;
      }
      if (cfg.hwAddress == 0 || cfg.hwAddress >= 40) {
        errMsg = "MOTOR_ALTERNATE requires hwAddress as direction GPIO pin (1-39)";
        return false;
      }
    }
  }

  if (cfg.type == ActuatorType::MOTOR_OPTICAL) {
    if (cfg.bus != HardwareBus::GPIO_DIRECT) {
      errMsg = "Motor optical currently requires GPIO_DIRECT (sensor read not available on PWM-only drivers)";
      return false;
    }
    if (cfg.behavior != ActuatorBehavior::MOTOR_OPTICAL_TRACK) {
      errMsg = "Motor optical requires behavior MOTOR_OPTICAL_TRACK";
      return false;
    }
    if (cfg.hwAddress > 39) {
      errMsg = "Motor optical sensor pin (hwAddress) must be in [0..39]";
      return false;
    }
    if (cfg.hwPin > 39) {
      errMsg = "Motor optical drive pin (hwPin) must be in [0..39]";
      return false;
    }
    if (cfg.hwPin == cfg.hwAddress) {
      errMsg = "Motor optical drive pin and sensor pin must be different";
      return false;
    }
  }

  errMsg = nullptr;
  return true;
}
