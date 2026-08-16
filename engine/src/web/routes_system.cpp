#include "web_server.h"
#include "../actuator/actuator_validation.h"
#include "../hal/i2c_scanner.h"
#include "../core/error_log.h"
#include "../core/panic_state.h"
#include "../core/status_led.h"
#include <LittleFS.h>

// --- System ---

void WebServerManager::_handleGetStatus(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();

  // WiFi
  obj["wifi_ssid"] = WiFi.SSID();
  obj["wifi_ip"] = WiFi.localIP().toString();
  obj["wifi_rssi"] = WiFi.RSSI();

  // MIDI
  obj["midi_connected"] = _midiEngine->isConnected();
  obj["midi_sessions"] = _midiEngine->getSessionCount();
  obj["midi_notes_rx"] = _midiEngine->getNotesReceived();
  obj["midi_notes_tx"] = _midiEngine->getNotesSent();
  obj["midi_channel_mask"] = _midiEngine->getChannelMask();

  // Actuators
  obj["actuator_count"] = _actMgr->getCount();
  obj["actuator_active"] = _actMgr->getActiveCount();
  obj["actuator_configs"] = _actFactory->getConfigCount();

  // Instruments
  obj["instrument_count"] = _instrMgr->getInstrumentCount();

  // Scheduler
  obj["scheduler_queue"] = _scheduler->queueCount();
  obj["scheduler_usage"] = _scheduler->queueUsage();
  obj["scheduler_dispatched"] = _scheduler->getDispatchCount();
  obj["scheduler_overflow"] = _scheduler->getOverflowCount();
  obj["scheduler_jitter_us"] = _scheduler->getLastJitterUs();
  obj["scheduler_max_jitter_us"] = _scheduler->getMaxJitterUs();

  // Event Engine
  obj["pipeline_count"] = _eventProc->getLookup().pipeline_count;
  obj["cc_route_count"] = _eventProc->getLookup().cc_route_count;
  obj["cc_route_dropped"] = _eventProc->getLookup().cc_route_dropped;

  const auto& ccStats = _eventProc->getCcDispatchStats();
  obj["cc_rx_total"] = ccStats.received;
  obj["cc_throttled_total"] = ccStats.throttled;
  obj["cc_routed_batches_total"] = ccStats.routed_batches;
  obj["cc_routed_commands_total"] = ccStats.routed_commands;
  obj["cc_unrouted_total"] = ccStats.unrouted;

  // Loops
  obj["loop_count"] = _loopEngine->getLoopCount();
  obj["loop_playing"] = _loopEngine->isPlaying();
  obj["loop_paused"] = _loopEngine->isPaused();
  if (_loopEngine->isPlaying()) {
    obj["loop_bpm"] = _loopEngine->getBpm();
    obj["loop_beat"] = _loopEngine->getCurrentBeat();
    obj["loop_bar"] = _loopEngine->getCurrentBar();
    obj["loop_tick"] = _loopEngine->getCurrentTick();
    obj["loop_index"] = _loopEngine->getCurrentLoopIndex();
  }
  obj["loop_chain_active"] = _loopEngine->isChainActive();
  if (_loopEngine->isChainActive()) {
    obj["loop_chain_index"] = _loopEngine->getChainIndex();
    obj["loop_chain_length"] = _loopEngine->getChainLength();
  }

  // System
  obj["free_heap"] = ESP.getFreeHeap();
  obj["min_free_heap"] = ESP.getMinFreeHeap();
  obj["uptime_s"] = millis() / 1000;
  obj["storage"] = _storage->formatInfo();
  obj["chip_model"] = ESP.getChipModel();
  obj["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
  obj["flash_size"] = ESP.getFlashChipSize();
  obj["sdk_version"] = ESP.getSdkVersion();
  obj["firmware_version"] = "1.0.0-beta";

  _sendJson(req, 200, doc);
}

void WebServerManager::_handleScanI2C(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["devices"] = I2CScanner::scan();
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleGetMidiNotes(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < GM_DRUM_NOTES_COUNT; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["note"] = GM_DRUM_NOTES[i].note;
    obj["name"] = GM_DRUM_NOTES[i].name;
  }
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleGetCapabilities(AsyncWebServerRequest* req) {
  JsonDocument doc;

  JsonArray types = doc["actuatorTypes"].to<JsonArray>();
  {
    JsonObject t = types.add<JsonObject>();
    t["id"] = (uint8_t)ActuatorType::SERVO;
    t["name"] = "Servo";
    t["recommendedBus"] = (uint8_t)HardwareBus::PCA9685;
    t["supportedBusMask"] = (1 << (uint8_t)HardwareBus::PCA9685);
    JsonArray sb = t["supportedBehaviors"].to<JsonArray>();
    sb.add((uint8_t)ActuatorBehavior::SERVO_POSITION);
    sb.add((uint8_t)ActuatorBehavior::SERVO_STRIKE);
    sb.add((uint8_t)ActuatorBehavior::COMB_BRUSH);
    sb.add((uint8_t)ActuatorBehavior::PITCH_BEND);
    sb.add((uint8_t)ActuatorBehavior::HIHAT_CONTROLLER);
  }
  {
    JsonObject t = types.add<JsonObject>();
    t["id"] = (uint8_t)ActuatorType::SOLENOID;
    t["name"] = "Solenoid";
    t["recommendedBus"] = (uint8_t)HardwareBus::MCP23017;
    t["supportedBusMask"] = (1 << (uint8_t)HardwareBus::MCP23017) |
                            (1 << (uint8_t)HardwareBus::GPIO_DIRECT);
  }
  {
    JsonObject t = types.add<JsonObject>();
    t["id"] = (uint8_t)ActuatorType::PWM_MOTOR;
    t["name"] = "PWM Motor";
    t["recommendedBus"] = (uint8_t)HardwareBus::LEDC_PWM;
    t["supportedBusMask"] = (1 << (uint8_t)HardwareBus::LEDC_PWM) |
                            (1 << (uint8_t)HardwareBus::PCA9685);
  }
  {
    JsonObject t = types.add<JsonObject>();
    t["id"] = (uint8_t)ActuatorType::MOTOR_OPTICAL;
    t["name"] = "Motor Optical";
    t["recommendedBus"] = (uint8_t)HardwareBus::GPIO_DIRECT;
    t["supportedBusMask"] = (1 << (uint8_t)HardwareBus::GPIO_DIRECT);
  }

  JsonArray behaviors = doc["actuatorBehaviors"].to<JsonArray>();
  for (uint8_t i = 0; i < (uint8_t)ActuatorBehavior::BEHAVIOR_COUNT; i++) {
    JsonObject b = behaviors.add<JsonObject>();
    b["id"] = i;
    b["name"] = actuatorBehaviorName((ActuatorBehavior)i);
  }

  doc["maxActuators"] = MAX_ACTUATORS;
  doc["maxInstruments"] = MAX_INSTRUMENTS;
  doc["maxActuatorsPerInstrument"] = MAX_ACTUATORS_PER_INST;
  doc["maxPipelines"] = MAX_PIPELINES;
  doc["maxCcRoutes"] = MAX_CC_ROUTES;
  doc["midiChannelCount"] = MIDI_CHANNEL_COUNT;

  // --- Hardware map (consumed by the "Cablage" page to generate the wiring
  // report). Everything here is a compile-time constant of the firmware, so the
  // UI never has to hardcode a pin number that could drift from config.h.
  JsonObject hw = doc["hardware"].to<JsonObject>();
  hw["board"] = "ESP32 (WROOM-32)";
  hw["i2cSda"] = I2C_SDA;
  hw["i2cScl"] = I2C_SCL;
  hw["i2cFreq"] = I2C_FREQ;
  hw["mcpBaseAddress"] = MCP_BASE_ADDRESS;
  hw["mcpMaxModules"] = MCP_MAX_MODULES;
  hw["pcaBaseAddress"] = PCA_BASE_ADDRESS;
  hw["pcaMaxModules"] = PCA_MAX_MODULES;
  hw["pcaFrequency"] = PCA_FREQUENCY;
  hw["statusLedPin"] = STATUS_LED_PIN;
  hw["bootButtonPin"] = BOOT_BUTTON_PIN;
  hw["micSck"] = MIC_I2S_SCK;
  hw["micWs"] = MIC_I2S_WS;
  hw["micSd"] = MIC_I2S_SD;
  // Live values, not the compile-time defaults: the power budget is now
  // configurable at runtime via /api/power-budget.
  hw["maxConcurrentActive"] = _actMgr->getPowerBudget().maxConcurrent;
  hw["powerBudgetPeakMa"] = _actMgr->getPowerBudget().maxPeakMa;
  hw["powerBudgetContinuousMa"] = _actMgr->getPowerBudget().maxContinuousMa;
  hw["solenoidMaxOnMs"] = SOLENOID_MAX_ON_US / 1000;
  hw["motorMaxContinuousMs"] = MOTOR_MAX_CONTINUOUS_US / 1000;
  hw["stepperMaxSps"] = STEPPER_MAX_SPEED_SPS;
  hw["ledMaxStrips"] = LED_MAX_STRIPS;

  // Pins the engine reserves for itself. Same table the config validator uses,
  // so the wiring page and the backend can never disagree about which GPIO is
  // free. `exclusive` entries are rejected outright when configuring an
  // actuator; the others belong to optional peripherals and are advisory.
  JsonArray reserved = hw["reservedPins"].to<JsonArray>();
  uint8_t reservedCount = 0;
  const PinReservation* reservations = esp32PinReservations(reservedCount);
  for (uint8_t i = 0; i < reservedCount; i++) {
    JsonObject r = reserved.add<JsonObject>();
    r["pin"] = reservations[i].pin;
    r["function"] = reservations[i].function;
    r["label"] = reservations[i].label;
    r["exclusive"] = reservations[i].exclusive;
  }

  // Velocity curves
  JsonArray curves = doc["velocityCurves"].to<JsonArray>();
  { JsonObject c = curves.add<JsonObject>(); c["id"] = CURVE_LINEAR; c["name"] = "Linear"; c["description"] = "Direct 1:1 mapping"; }
  { JsonObject c = curves.add<JsonObject>(); c["id"] = CURVE_EXPO; c["name"] = "Exponential"; c["description"] = "Faster rise (v^2)"; }
  { JsonObject c = curves.add<JsonObject>(); c["id"] = CURVE_LOG; c["name"] = "Logarithmic"; c["description"] = "Slower rise (sqrt)"; }

  _sendJson(req, 200, doc);
}

void WebServerManager::_handleGetCcRoutes(AsyncWebServerRequest* req) {
  const PipelineLookup& lookup = _eventProc->getLookup();
  JsonDocument doc;
  doc["count"] = lookup.cc_route_count;
  doc["max"] = MAX_CC_ROUTES;
  doc["dropped"] = lookup.cc_route_dropped;

  JsonArray routes = doc["routes"].to<JsonArray>();
  for (uint8_t cc = 0; cc < 128; cc++) {
    uint8_t first = lookup.cc_to_first[cc];
    uint8_t count = lookup.cc_to_count[cc];
    if (first == 0xFF || count == 0) continue;

    JsonObject ccObj = routes.add<JsonObject>();
    ccObj["cc"] = cc;
    JsonArray entries = ccObj["entries"].to<JsonArray>();

    for (uint8_t i = 0; i < count; i++) {
      uint8_t routeIdx = first + i;
      if (routeIdx >= lookup.cc_route_count) break;
      const CCRoutingEntry& route = lookup.cc_routes[routeIdx];

      JsonObject e = entries.add<JsonObject>();
      e["actuatorId"] = route.actuator_id;
      e["commandType"] = route.command_type;
      e["rangeMin"] = route.range_min;
      e["rangeMax"] = route.range_max;
      e["curve"] = route.curve;
      e["inverted"] = route.inverted;
    }
  }

  _sendJson(req, 200, doc);
}

void WebServerManager::_handleGetTemplates(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  JsonObject t1 = arr.add<JsonObject>();
  t1["id"] = "kick";
  t1["name"] = "Kick simple";
  t1["midiNote"] = 36;
  t1["category"] = "drums";
  t1["actuatorSlots"] = 1;
  JsonArray kickHints = t1["slotHints"].to<JsonArray>();
  JsonObject h1 = kickHints.add<JsonObject>();
  h1["slot"] = 0;
  h1["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h1["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;

  JsonObject t2 = arr.add<JsonObject>();
  t2["id"] = "hihat";
  t2["name"] = "Hi-Hat (CC4 + servo)";
  t2["midiNote"] = 42;
  t2["category"] = "cymbals";
  t2["actuatorSlots"] = 2;
  JsonArray hihatHints = t2["slotHints"].to<JsonArray>();
  JsonObject h2a = hihatHints.add<JsonObject>();
  h2a["slot"] = 0;
  h2a["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h2a["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;
  JsonObject h2b = hihatHints.add<JsonObject>();
  h2b["slot"] = 1;
  h2b["requiredType"] = (uint8_t)ActuatorType::SERVO;
  h2b["requiredBehavior"] = (uint8_t)ActuatorBehavior::HIHAT_CONTROLLER;

  JsonObject t3 = arr.add<JsonObject>();
  t3["id"] = "cymbal_mute";
  t3["name"] = "Cymbal + mute";
  t3["midiNote"] = 49;
  t3["category"] = "cymbals";
  t3["actuatorSlots"] = 2;
  JsonArray cymbalHints = t3["slotHints"].to<JsonArray>();
  JsonObject h3a = cymbalHints.add<JsonObject>();
  h3a["slot"] = 0;
  h3a["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h3a["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;
  JsonObject h3b = cymbalHints.add<JsonObject>();
  h3b["slot"] = 1;
  h3b["requiredType"] = (uint8_t)ActuatorType::SERVO;
  h3b["requiredBehavior"] = (uint8_t)ActuatorBehavior::SERVO_POSITION;

  JsonObject t4 = arr.add<JsonObject>();
  t4["id"] = "double_hit";
  t4["name"] = "Tom double frappe";
  t4["midiNote"] = 45;
  t4["category"] = "drums";
  t4["actuatorSlots"] = 2;
  JsonArray tomHints = t4["slotHints"].to<JsonArray>();
  JsonObject h4a = tomHints.add<JsonObject>();
  h4a["slot"] = 0;
  h4a["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h4a["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;
  JsonObject h4b = tomHints.add<JsonObject>();
  h4b["slot"] = 1;
  h4b["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h4b["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;

  JsonObject t5 = arr.add<JsonObject>();
  t5["id"] = "shaker";
  t5["name"] = "Shaker / Maracas";
  t5["midiNote"] = 82;
  t5["category"] = "latin";
  t5["actuatorSlots"] = 1;
  JsonArray shakerHints = t5["slotHints"].to<JsonArray>();
  JsonObject h5 = shakerHints.add<JsonObject>();
  h5["slot"] = 0;
  h5["requiredType"] = (uint8_t)ActuatorType::PWM_MOTOR;

  JsonObject t6 = arr.add<JsonObject>();
  t6["id"] = "brush";
  t6["name"] = "Brush (Comb)";
  t6["midiNote"] = 38;
  t6["category"] = "drums";
  t6["actuatorSlots"] = 1;
  JsonArray brushHints = t6["slotHints"].to<JsonArray>();
  JsonObject h6 = brushHints.add<JsonObject>();
  h6["slot"] = 0;
  h6["requiredType"] = (uint8_t)ActuatorType::PWM_MOTOR;

  JsonObject t7 = arr.add<JsonObject>();
  t7["id"] = "timpani";
  t7["name"] = "Timpani pitch";
  t7["midiNote"] = 47;
  t7["category"] = "drums";
  t7["actuatorSlots"] = 2;
  JsonArray timpaniHints = t7["slotHints"].to<JsonArray>();
  JsonObject h7a = timpaniHints.add<JsonObject>();
  h7a["slot"] = 0;
  h7a["requiredType"] = (uint8_t)ActuatorType::SOLENOID;
  h7a["requiredBehavior"] = (uint8_t)ActuatorBehavior::SOLENOID_STRIKE;
  JsonObject h7b = timpaniHints.add<JsonObject>();
  h7b["slot"] = 1;
  h7b["requiredType"] = (uint8_t)ActuatorType::SERVO;
  h7b["requiredBehavior"] = (uint8_t)ActuatorBehavior::PITCH_BEND;
  JsonObject t8 = arr.add<JsonObject>();
  t8["id"] = "motor_optical";
  t8["name"] = "Motor + capteur optique";
  t8["midiNote"] = 60;
  t8["category"] = "other";
  t8["actuatorSlots"] = 1;
  JsonArray hints = t8["slotHints"].to<JsonArray>();
  JsonObject h0 = hints.add<JsonObject>();
  h0["slot"] = 0;
  h0["requiredType"] = (uint8_t)ActuatorType::MOTOR_OPTICAL;
  h0["requiredBehavior"] = (uint8_t)ActuatorBehavior::MOTOR_OPTICAL_TRACK;


  _sendJson(req, 200, doc);
}

void WebServerManager::_handleCreateInstrumentFromTemplate(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) {
    _sendError(req, 400, "Invalid JSON");
    return;
  }

  const char* tpl = doc["template"] | "";
  if (!tpl || tpl[0] == '\0') {
    _sendError(req, 400, "template required");
    return;
  }

  uint8_t requestedActuators[3] = {0xFF, 0xFF, 0xFF};
  JsonArray reqActuatorIds = doc["actuatorIds"].as<JsonArray>();
  if (!reqActuatorIds.isNull()) {
    uint8_t idx = 0;
    for (JsonVariant v : reqActuatorIds) {
      if (idx >= 3) break;
      requestedActuators[idx++] = v | 0xFF;
    }
  }
  requestedActuators[0] = doc["actuatorId"] | requestedActuators[0];
  requestedActuators[0] = doc["primaryActuatorId"] | requestedActuators[0];
  requestedActuators[1] = doc["secondaryActuatorId"] | requestedActuators[1];
  requestedActuators[2] = doc["tertiaryActuatorId"] | requestedActuators[2];

  auto validateTemplateSlot = [this](uint8_t actuatorId,
                                     ActuatorType requiredType,
                                     ActuatorBehavior requiredBehavior,
                                     const char*& errMsg) -> bool {
    if (actuatorId == 0xFF) {
      errMsg = "Template requires actuatorIds mapping";
      return false;
    }
    ActuatorConfig* cfg = _actFactory->findConfig(actuatorId);
    if (!cfg) {
      errMsg = "Template slot references unknown actuatorId";
      return false;
    }
    if (cfg->type != requiredType) {
      errMsg = "Template slot actuator type mismatch";
      return false;
    }
    if ((uint8_t)requiredBehavior < (uint8_t)ActuatorBehavior::BEHAVIOR_COUNT &&
        cfg->behavior != requiredBehavior) {
      errMsg = "Template slot actuator behavior mismatch";
      return false;
    }
    return true;
  };

  const char* templateErr = nullptr;
  auto requireSlot = [&](uint8_t slot,
                         ActuatorType type,
                         ActuatorBehavior behavior) -> bool {
    if (slot >= 3) {
      templateErr = "Invalid template slot index";
      return false;
    }
    return validateTemplateSlot(requestedActuators[slot], type, behavior, templateErr);
  };

  if (strcmp(tpl, "kick") == 0) {
    if (!requireSlot(0, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "hihat") == 0) {
    if (!requireSlot(0, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE) ||
        !requireSlot(1, ActuatorType::SERVO, ActuatorBehavior::SERVO_POSITION)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "cymbal_mute") == 0) {
    if (!requireSlot(0, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE) ||
        !requireSlot(1, ActuatorType::SERVO, ActuatorBehavior::SERVO_POSITION)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "double_hit") == 0) {
    if (!requireSlot(0, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE) ||
        !requireSlot(1, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "shaker") == 0) {
    if (!requireSlot(0, ActuatorType::PWM_MOTOR, ActuatorBehavior::BEHAVIOR_COUNT)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "brush") == 0) {
    if (!requireSlot(0, ActuatorType::PWM_MOTOR, ActuatorBehavior::BEHAVIOR_COUNT)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "timpani") == 0) {
    if (!requireSlot(0, ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE) ||
        !requireSlot(1, ActuatorType::SERVO, ActuatorBehavior::PITCH_BEND)) {
      _sendError(req, 400, templateErr);
      return;
    }
  } else if (strcmp(tpl, "motor_optical") == 0) {
    if (!requireSlot(0, ActuatorType::MOTOR_OPTICAL, ActuatorBehavior::MOTOR_OPTICAL_TRACK)) {
      _sendError(req, 400, templateErr);
      return;
    }
  }

  InstrumentConfig inst;
  strlcpy(inst.name, doc["name"] | "Template Instrument", sizeof(inst.name));
  strlcpy(inst.category, doc["category"] | "drums", sizeof(inst.category));
  inst.midiNote = doc["midiNote"] | 36;
  inst.midiChannel = doc["midiChannel"] | 10;
  inst.enabled = true;

  if (strcmp(tpl, "kick") == 0) {
    strlcpy(inst.name, "Kick simple", sizeof(inst.name));
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].duration_min_ms = 8;
    inst.noteOnActions[0].duration_max_ms = 25;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.noteOffCount = 1;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].actuator_id = requestedActuators[0];
  } else if (strcmp(tpl, "hihat") == 0) {
    strlcpy(inst.name, "Hi-Hat", sizeof(inst.name));
    strlcpy(inst.category, "cymbals", sizeof(inst.category));
    inst.noteOnCount = 2;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].duration_min_ms = 8;
    inst.noteOnActions[0].duration_max_ms = 25;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.noteOnActions[1].command_type = (uint8_t)CommandType::POSITION;
    inst.noteOnActions[1].value_source = (uint8_t)ValueSource::CC_VAR;
    inst.noteOnActions[1].cc_index = 4;
    inst.noteOnActions[1].actuator_id = requestedActuators[1];

    inst.ccBindingCount = 1;
    inst.ccBindings[0].cc_number = 4;
    inst.ccBindings[0].command_type = (uint8_t)CommandType::POSITION;
    inst.ccBindings[0].range_min = 30;
    inst.ccBindings[0].range_max = 150;
    inst.ccBindings[0].actuator_id = requestedActuators[1];
  } else if (strcmp(tpl, "cymbal_mute") == 0) {
    strlcpy(inst.name, "Cymbal + mute", sizeof(inst.name));
    strlcpy(inst.category, "cymbals", sizeof(inst.category));
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].duration_min_ms = 8;
    inst.noteOnActions[0].duration_max_ms = 20;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];

    inst.noteOffCount = 2;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::POSITION;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].value_fixed = 20;
    inst.noteOffActions[0].delay_ms = 50;
    inst.noteOffActions[0].actuator_id = requestedActuators[1];
    inst.noteOffActions[1].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[1].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[1].actuator_id = requestedActuators[0];
  } else if (strcmp(tpl, "double_hit") == 0) {
    strlcpy(inst.name, "Tom double frappe", sizeof(inst.name));
    strlcpy(inst.category, "drums", sizeof(inst.category));
    inst.noteOnCount = 2;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].duration_min_ms = 8;
    inst.noteOnActions[0].duration_max_ms = 22;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.noteOnActions[1] = inst.noteOnActions[0];
    inst.noteOnActions[1].actuator_id = requestedActuators[1];
    inst.noteOffCount = 1;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].actuator_id = requestedActuators[0];
  } else if (strcmp(tpl, "shaker") == 0) {
    strlcpy(inst.name, "Shaker / Maracas", sizeof(inst.name));
    strlcpy(inst.category, "latin", sizeof(inst.category));
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PWM;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.noteOffCount = 1;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].actuator_id = requestedActuators[0];
  } else if (strcmp(tpl, "brush") == 0) {
    strlcpy(inst.name, "Brush", sizeof(inst.name));
    strlcpy(inst.category, "drums", sizeof(inst.category));
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PWM;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.ccBindingCount = 1;
    inst.ccBindings[0].cc_number = 1;
    inst.ccBindings[0].command_type = (uint8_t)CommandType::PWM;
    inst.ccBindings[0].range_min = 20;
    inst.ccBindings[0].range_max = 127;
    inst.ccBindings[0].actuator_id = requestedActuators[0];
    inst.noteOffCount = 1;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].actuator_id = requestedActuators[0];
  } else if (strcmp(tpl, "timpani") == 0) {
    strlcpy(inst.name, "Timpani", sizeof(inst.name));
    strlcpy(inst.category, "drums", sizeof(inst.category));
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].duration_min_ms = 10;
    inst.noteOnActions[0].duration_max_ms = 30;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.ccBindingCount = 1;
    inst.ccBindings[0].cc_number = 127;
    inst.ccBindings[0].command_type = (uint8_t)CommandType::POSITION;
    inst.ccBindings[0].range_min = 30;
    inst.ccBindings[0].range_max = 110;
    inst.ccBindings[0].actuator_id = (requestedActuators[1] == 0xFF) ? requestedActuators[0] : requestedActuators[1];
  } else if (strcmp(tpl, "motor_optical") == 0) {
    strlcpy(inst.name, "Motor Optical", sizeof(inst.name));
    strlcpy(inst.category, "other", sizeof(inst.category));
    inst.midiNote = doc["midiNote"] | 60;
    inst.noteOnCount = 1;
    inst.noteOnActions[0].command_type = (uint8_t)CommandType::POSITION;
    inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
    inst.noteOnActions[0].actuator_id = requestedActuators[0];
    inst.noteOffCount = 1;
    inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
    inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
    inst.noteOffActions[0].actuator_id = requestedActuators[0];
  } else {
    _sendError(req, 400, "Unknown template");
    return;
  }

  const char* errMsg = nullptr;
  if (!_validateInstrumentConfig(inst, errMsg)) {
    _sendError(req, 400, errMsg ? errMsg : "Invalid template instrument config");
    return;
  }

  int idx = _instrMgr->addInstrument(inst);
  if (idx < 0) {
    _sendError(req, 507, "Max instruments reached");
    return;
  }

  _recompileLookupFromInstruments();
  _storage->saveInstruments(*_instrMgr);

  JsonDocument resp;
  JsonObject obj = resp.to<JsonObject>();
  _instrMgr->instrumentToJson(*_instrMgr->getInstrumentByIndex(idx), obj);
  _sendJson(req, 201, resp);
}

void WebServerManager::_handleSaveConfig(AsyncWebServerRequest* req) {
  // H6 fix: Check return values from save operations
  bool ok1 = _storage->saveActuators(*_actFactory);
  bool ok2 = _storage->saveInstruments(*_instrMgr);
  if (!ok1 || !ok2) {
    _sendError(req, 500, "Save failed (storage write error)");
    return;
  }
  _sendError(req, 200, "Configuration saved");
}

// --- Logs API ---

void WebServerManager::_handleGetLogs(AsyncWebServerRequest* req) {
  JsonDocument doc;

  // Ring buffer (recent entries)
  uint16_t count = 0;
  const LogEntry* entries = ErrorLog::getEntries(count);
  JsonArray recent = doc["recent"].to<JsonArray>();

  // Ring buffer is circular: output in chronological order
  uint16_t ringHead = count < ERROR_LOG_RING_SIZE ? 0 : (ErrorLog::entryCount() % ERROR_LOG_RING_SIZE);
  for (uint16_t i = 0; i < count; i++) {
    uint16_t idx = (ringHead + i) % ERROR_LOG_RING_SIZE;
    const LogEntry& e = entries[idx];
    JsonObject obj = recent.add<JsonObject>();
    obj["uptimeS"] = e.uptimeS;
    obj["level"] = e.level;
    obj["message"] = e.message;
  }

  doc["entryCount"] = ErrorLog::entryCount();

  // Include full log if requested with ?full=1
  if (req->hasParam("full") && req->getParam("full")->value() == "1") {
    doc["full"] = ErrorLog::readAll();
  }

  _sendJson(req, 200, doc);
}

void WebServerManager::_handleClearLogs(AsyncWebServerRequest* req) {
  ErrorLog::clear();
  _sendError(req, 200, "Logs cleared");
}

void WebServerManager::_handleGetAuthToken(AsyncWebServerRequest* req) {
  // H7 fix: No-PIN case (GET without PIN) — return token directly
  if (!_auth.hasPin()) {
    JsonDocument doc;
    doc["token"] = _auth.getToken();
    doc["pinRequired"] = false;
    _sendJson(req, 200, doc);
    return;
  }
  // PIN is required — must use POST with PIN in body
  JsonDocument doc;
  doc["error"] = "PIN required (use POST with {pin} in body)";
  doc["pinRequired"] = true;
  _sendJson(req, 401, doc);
}

void WebServerManager::_handlePostAuthToken(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  // H7 fix: Accept PIN in POST body instead of query string
  if (!_auth.hasPin()) {
    JsonDocument doc;
    doc["token"] = _auth.getToken();
    doc["pinRequired"] = false;
    _sendJson(req, 200, doc);
    return;
  }
  JsonDocument body;
  if (deserializeJson(body, data, len)) {
    _sendError(req, 400, "Invalid JSON");
    return;
  }
  const char* pin = body["pin"];
  if (!pin || !_auth.checkPin(String(pin))) {
    JsonDocument doc;
    doc["error"] = "Invalid PIN";
    doc["pinRequired"] = true;
    _sendJson(req, 401, doc);
    return;
  }
  JsonDocument doc;
  doc["token"] = _auth.getToken();
  doc["pinRequired"] = true;
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleLoginStatus(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["pinRequired"] = _auth.hasPin();
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleSetPin(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) {
    _sendError(req, 400, "Invalid JSON");
    return;
  }

  // If a PIN is already set, require current PIN to change it
  if (_auth.hasPin()) {
    const char* currentPin = doc["currentPin"];
    if (!currentPin || !_auth.checkPin(String(currentPin))) {
      _sendError(req, 401, "Current PIN is incorrect");
      return;
    }
  }

  const char* newPin = doc["pin"];
  if (!newPin) {
    // Remove PIN
    _auth.setPin("");
    JsonDocument resp;
    resp["message"] = "PIN removed";
    resp["pinRequired"] = false;
    _sendJson(req, 200, resp);
    return;
  }

  String pinStr(newPin);
  if (pinStr.length() < 4 || pinStr.length() > 16) {
    _sendError(req, 400, "PIN must be 4-16 characters");
    return;
  }

  if (_auth.setPin(pinStr)) {
    JsonDocument resp;
    resp["message"] = "PIN set successfully";
    resp["pinRequired"] = true;
    _sendJson(req, 200, resp);
  } else {
    _sendError(req, 500, "Failed to save PIN");
  }
}

void WebServerManager::_handleGetMidiChannels(AsyncWebServerRequest* req) {
  JsonDocument doc;
  uint16_t mask = _midiEngine->getChannelMask();
  doc["channelMask"] = mask;
  JsonArray channels = doc["channels"].to<JsonArray>();
  for (uint8_t ch = 1; ch <= 16; ch++) {
    if ((mask >> (ch - 1)) & 1) channels.add(ch);
  }
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleSetMidiChannels(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }

  uint16_t mask = 0;
  if (doc.containsKey("channelMask")) {
    mask = doc["channelMask"] | 0xFFFF;
  } else if (doc.containsKey("channels")) {
    JsonArray channels = doc["channels"].as<JsonArray>();
    for (JsonVariant ch : channels) {
      uint8_t c = ch.as<uint8_t>();
      if (c >= 1 && c <= 16) mask |= (1 << (c - 1));
    }
  } else {
    _sendError(req, 400, "Provide channelMask or channels array");
    return;
  }
  if (mask == 0) { _sendError(req, 400, "At least one channel required"); return; }

  _midiEngine->setChannelMask(mask);

  // Persist to /midi.json
  JsonDocument saveDoc;
  saveDoc["channelMask"] = mask;
  _storage->saveJsonFile("/midi.json", saveDoc);

  JsonDocument resp;
  resp["channelMask"] = mask;
  resp["message"] = "MIDI channels updated";
  _sendJson(req, 200, resp);
}

// GET /api/power-budget
// Etat du budget electrique : limites configurees, consommation instantanee,
// pire cas si tout se declenchait ensemble, et refus comptabilises par motif.
void WebServerManager::_handleGetPowerBudget(AsyncWebServerRequest* req) {
  const PowerBudgetConfig& budget = _actMgr->getPowerBudget();

  JsonDocument doc;
  doc["maxPeakMa"] = budget.maxPeakMa;
  doc["maxContinuousMa"] = budget.maxContinuousMa;
  doc["maxConcurrent"] = budget.maxConcurrent;

  doc["activeCurrentMa"] = _actMgr->getActiveCurrentMa();
  doc["activeCount"] = _actMgr->getActiveCount();
  uint32_t worstCase = _actMgr->getWorstCaseCurrentMa();
  doc["worstCaseCurrentMa"] = worstCase;
  // Le pire cas depasse-t-il ce que l'alimentation declaree peut fournir ? Si
  // oui, l'arbitrage FERA son travail (des frappes seront refusees) : c'est un
  // avertissement de dimensionnement, pas une erreur.
  doc["worstCaseExceedsPeak"] = (budget.maxPeakMa > 0 && worstCase > budget.maxPeakMa);

  JsonObject refused = doc["refused"].to<JsonObject>();
  refused["byCount"] = _actMgr->getRefusedByCount();
  refused["byPeak"] = _actMgr->getRefusedByPeak();
  refused["byContinuous"] = _actMgr->getRefusedByContinuous();

  JsonArray priorities = doc["priorities"].to<JsonArray>();
  for (uint8_t p = 0; p < (uint8_t)ActuatorPriority::PRIORITY_COUNT; p++) {
    JsonObject o = priorities.add<JsonObject>();
    o["id"] = p;
    o["name"] = actuatorPriorityName(p);
  }

  _sendJson(req, 200, doc);
}

// PUT /api/power-budget  {maxPeakMa, maxContinuousMa, maxConcurrent}
void WebServerManager::_handleSetPowerBudget(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }

  PowerBudgetConfig budget = _actMgr->getPowerBudget();
  if (doc.containsKey("maxPeakMa"))       budget.maxPeakMa = doc["maxPeakMa"] | 0;
  if (doc.containsKey("maxContinuousMa")) budget.maxContinuousMa = doc["maxContinuousMa"] | 0;
  if (doc.containsKey("maxConcurrent"))   budget.maxConcurrent = doc["maxConcurrent"] | 0;

  if (budget.maxConcurrent > MAX_ACTUATORS) {
    _sendError(req, 400, "maxConcurrent cannot exceed MAX_ACTUATORS");
    return;
  }
  // Un plafond continu au-dessus du plafond crete serait inatteignable : il ne
  // declasserait jamais rien, ce qui n'est presque jamais l'intention.
  if (budget.maxPeakMa > 0 && budget.maxContinuousMa > budget.maxPeakMa) {
    _sendError(req, 400, "maxContinuousMa must be <= maxPeakMa");
    return;
  }

  _actMgr->setPowerBudget(budget);

  JsonDocument saveDoc;
  saveDoc["maxPeakMa"] = budget.maxPeakMa;
  saveDoc["maxContinuousMa"] = budget.maxContinuousMa;
  saveDoc["maxConcurrent"] = budget.maxConcurrent;
  _storage->saveJsonFile(POWER_FILE, saveDoc);

  JsonDocument resp;
  resp["maxPeakMa"] = budget.maxPeakMa;
  resp["maxContinuousMa"] = budget.maxContinuousMa;
  resp["maxConcurrent"] = budget.maxConcurrent;
  resp["message"] = "Power budget updated";
  _sendJson(req, 200, resp);
}

void WebServerManager::_handleTestCC(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }
  uint8_t cc = doc["cc"] | 7;
  uint8_t val = doc["value"] | 127;
  // CC routing is channel aware, so the test endpoint has to be too: hardcoding
  // channel 10 made it impossible to exercise a CC binding that belongs to an
  // instrument on any other channel.
  uint8_t channel = doc["channel"] | 10;
  if (channel < 1 || channel > MIDI_CHANNEL_COUNT) {
    _sendError(req, 400, "channel must be in [1..16]");
    return;
  }

  // Send CC event directly to EventProcessor
  MidiEvent ev;
  ev.type = MIDI_EVT_CC;
  ev.channel = channel;
  ev.data1 = cc;
  ev.data2 = val;
  ev.timestamp = micros();

  if (_eventProc) {
    _eventProc->processMidiEvent(ev);
  }

  _sendError(req, 200, "CC sent");
}

void WebServerManager::_handleTestNote(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }
  uint8_t note = doc["note"] | 60;
  uint8_t velocity = doc["velocity"] | 80;
  uint8_t channel = doc["channel"] | 10;
  bool noteOff = doc["noteOff"] | false;

  // Routing rejects an out-of-range channel outright rather than folding it onto
  // channel 1. Say so, instead of accepting the request and doing nothing.
  if (channel < 1 || channel > MIDI_CHANNEL_COUNT) {
    _sendError(req, 400, "channel must be in [1..16]");
    return;
  }
  if (note >= 128) {
    _sendError(req, 400, "note must be in [0..127]");
    return;
  }

  MidiEvent ev;
  ev.type = noteOff ? MIDI_EVT_NOTE_OFF : MIDI_EVT_NOTE_ON;
  ev.channel = channel;
  ev.data1 = note;
  ev.data2 = velocity;
  ev.timestamp = micros();

  if (_eventProc) {
    _eventProc->processMidiEvent(ev);
  }

  _sendError(req, 200, noteOff ? "NoteOff sent" : "NoteOn sent");
}

void WebServerManager::_handleGetActiveNotes(AsyncWebServerRequest* req) {
  uint8_t notes[128];
  _eventProc->getNoteActive(notes);

  // Return compact array of active note numbers (lightweight for polling)
  JsonDocument doc;
  JsonArray arr = doc["active"].to<JsonArray>();
  for (uint8_t i = 0; i < 128; i++) {
    if (notes[i] > 0) arr.add(i);
  }
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleValidateConfig(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray issues = doc["issues"].to<JsonArray>();
  JsonArray warnings = doc["warnings"].to<JsonArray>();
  JsonArray ok = doc["ok"].to<JsonArray>();

  uint8_t actCfgCount = _actFactory->getConfigCount();
  uint8_t instrCount = _instrMgr->getInstrumentCount();

  // Check actuators
  if (actCfgCount == 0) {
    issues.add("No actuators configured");
  } else {
    uint8_t disabledAct = 0;
    for (uint8_t i = 0; i < actCfgCount; i++) {
      const ActuatorConfig& cfg = _actFactory->getConfig(i);
      if (!cfg.enabled) disabledAct++;
    }
    if (disabledAct > 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%d actuator(s) disabled", disabledAct);
      warnings.add(buf);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%d actuator(s) configured", actCfgCount);
    ok.add(buf);
  }

  // Check instruments
  if (instrCount == 0) {
    issues.add("No instruments configured");
  } else {
    uint8_t noActions = 0;
    uint8_t disabledInstr = 0;
    // Check for duplicate MIDI notes (use heap to avoid 2KB stack allocation)
    uint8_t* noteCh = (uint8_t*)calloc(128 * 16, sizeof(uint8_t));
    for (uint8_t i = 0; i < instrCount; i++) {
      const InstrumentConfig* instr = _instrMgr->getInstrumentByIndex(i);
      if (!instr) continue;
      if (!instr->enabled) disabledInstr++;
      if (instr->noteOnCount == 0) noActions++;
      uint8_t ch = instr->midiChannel < 16 ? instr->midiChannel : 0;
      if (instr->midiNote < 128 && noteCh) noteCh[instr->midiNote * 16 + ch]++;
    }
    if (noActions > 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%d instrument(s) without NoteOn actions", noActions);
      warnings.add(buf);
    }
    if (disabledInstr > 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%d instrument(s) disabled", disabledInstr);
      warnings.add(buf);
    }
    if (noteCh) {
      for (uint8_t n = 0; n < 128; n++) {
        for (uint8_t c = 0; c < 16; c++) {
          if (noteCh[n * 16 + c] > 1) {
            char buf[80];
            snprintf(buf, sizeof(buf), "Duplicate MIDI note %d on channel %d (%d instruments)", n, c + 1, noteCh[n * 16 + c]);
            warnings.add(buf);
          }
        }
      }
      free(noteCh);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%d instrument(s) configured", instrCount);
    ok.add(buf);
  }

  // Check for unused actuators
  if (actCfgCount > 0 && instrCount > 0) {
    uint8_t usedActuators = 0;
    for (uint8_t a = 0; a < actCfgCount; a++) {
      const ActuatorConfig& actCfg = _actFactory->getConfig(a);
      bool used = false;
      for (uint8_t i = 0; i < instrCount && !used; i++) {
        const InstrumentConfig* instr = _instrMgr->getInstrumentByIndex(i);
        if (!instr) continue;
        for (uint8_t j = 0; j < instr->noteOnCount && !used; j++) {
          if (instr->noteOnActions[j].actuator_id == actCfg.id) used = true;
        }
        for (uint8_t j = 0; j < instr->noteOffCount && !used; j++) {
          if (instr->noteOffActions[j].actuator_id == actCfg.id) used = true;
        }
        for (uint8_t j = 0; j < instr->ccBindingCount && !used; j++) {
          if (instr->ccBindings[j].actuator_id == actCfg.id) used = true;
        }
      }
      if (used) usedActuators++;
    }
    uint8_t unusedCount = actCfgCount - usedActuators;
    if (unusedCount > 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%d actuator(s) not used by any instrument", unusedCount);
      warnings.add(buf);
    }
  }

  // Memory check
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 20000) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Low memory: %d KB free", freeHeap / 1024);
    issues.add(buf);
  } else if (freeHeap < 50000) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Memory moderate: %d KB free", freeHeap / 1024);
    warnings.add(buf);
  }

  // Scheduler health
  if (_scheduler->getOverflowCount() > 0) {
    char buf[80];
    snprintf(buf, sizeof(buf), "Scheduler overflow detected (%lu events dropped)", _scheduler->getOverflowCount());
    warnings.add(buf);
  }
  if (_scheduler->getMaxJitterUs() > 1000) {
    char buf[80];
    snprintf(buf, sizeof(buf), "High timing jitter: %lu us max", _scheduler->getMaxJitterUs());
    warnings.add(buf);
  }

  // Pipeline check
  doc["pipeline_count"] = _eventProc->getLookup().pipeline_count;
  doc["cc_route_count"] = _eventProc->getLookup().cc_route_count;

  _sendJson(req, 200, doc);
}

void WebServerManager::_handleFactoryReset(AsyncWebServerRequest* req) {
  // Delete all configuration files
  const char* files[] = {
    CONFIG_FILE, ACTUATORS_FILE, INSTRUMENTS_FILE,
    LEDS_FILE, THEMES_FILE, "/midi.json", "/loops.json",
    "/error.log", AUTH_TOKEN_FILE
  };
  int deleted = 0;
  for (const char* f : files) {
    // Remove the file plus any crash-safe-write leftovers (.bak/.new), else a
    // stale backup would be "recovered" on next boot and defeat the reset.
    const char* suffixes[] = {"", ".bak", ".new"};
    for (const char* sfx : suffixes) {
      String p = String(f) + sfx;
      if (LittleFS.exists(p.c_str())) {
        LittleFS.remove(p.c_str());
        deleted++;
      }
    }
  }

  // Also clean loop files in /loops/ directory
  File dir = LittleFS.open(LOOPS_DIR);
  if (dir && dir.isDirectory()) {
    File file = dir.openNextFile();
    while (file) {
      String path = String(LOOPS_DIR) + "/" + file.name();
      file.close();
      LittleFS.remove(path.c_str());
      deleted++;
      file = dir.openNextFile();
    }
    dir.close();
  }

  DBGF("[System] Factory reset: %d files deleted\n", deleted);
  _sendError(req, 200, "Factory reset done, rebooting...");

  // Delay then reboot
  delay(500);
  ESP.restart();
}

// ============================================================================
// Emergency Stop / Panic (P0 #3)
// ============================================================================
// Immediately and reliably stops all motion. Unlike sending value=0 test
// commands (which a solenoid interprets as a min-duration pulse, a servo as
// its min position, and a motor as remapped PWM), this drives every actuator
// OFF directly and clears everything that could re-trigger them.
//
// Order matters:
//   1. Stop loop playback   -> no new loop-generated NoteOn events.
//   2. Clear scheduler queue -> pending ON commands never dispatch.
//   3. stopAll() actuators   -> direct hardware OFF, bypassing the queue.
//   4. Reset NoteOn/retrigger state -> next NoteOn re-triggers cleanly.
//   5. Refresh the active-count cache.
// The stop path never goes through the normal command queue, so it stays
// effective even when the queue is saturated.
void WebServerManager::_handlePanic(AsyncWebServerRequest* req) {
  // 0. Latch the panic state FIRST so that any activation racing on Core 1
  //    (a NoteOn arriving between the steps below) is refused by the guards in
  //    EventProcessor / ActuatorManager / LoopEngine. The latch stays set until
  //    an explicit POST /api/panic/reset.
  g_panicActive.store(true, std::memory_order_release);

  // 1. Halt loop playback (Core 0) so no further events are generated.
  if (_loopEngine) _loopEngine->stop();

  // 2. Drop every pending command so no queued ON fires after we stop.
  if (_scheduler) _scheduler->cancelAll();

  // 3. Force all actuators OFF directly (not via the queue).
  if (_actMgr) {
    _actMgr->stopAll();
    _actMgr->updateActiveCount();
  }

  // 4. Reset NoteOn / retrigger counters so a subsequent NoteOn works.
  if (_eventProc) _eventProc->panicReset();

  DBGLN("[System] PANIC: all actuators stopped, queue cleared, loops halted, LATCHED");
  ErrorLog::info("Panic: emergency stop triggered (latched)");

  JsonDocument doc;
  doc["message"] = "Emergency stop: all actuators halted. System latched — POST /api/panic/reset to re-arm.";
  doc["stopped"] = true;
  doc["latched"] = true;
  _sendJson(req, 200, doc);
}

// Re-arm the system after an emergency stop (P0 #3). Explicit and deliberate:
// auto-clearing the latch would reopen the activation race the latch closes.
void WebServerManager::_handlePanicReset(AsyncWebServerRequest* req) {
  g_panicActive.store(false, std::memory_order_release);
  DBGLN("[System] Panic latch cleared — system re-armed");
  ErrorLog::info("Panic latch cleared (re-armed)");

  JsonDocument doc;
  doc["message"] = "System re-armed";
  doc["latched"] = false;
  _sendJson(req, 200, doc);
}
