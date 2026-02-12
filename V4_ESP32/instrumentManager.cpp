#include "instrumentManager.h"

InstrumentManager::InstrumentManager(ActuatorDriver* driver, Scheduler* scheduler)
  : _driver(driver), _scheduler(scheduler), _count(0) {
  for (int i = 0; i < 128; i++) _noteMap[i] = -1;
}

// --- CRUD Instruments ---

int InstrumentManager::addInstrument(const InstrumentConfig& cfg) {
  if (_count >= MAX_INSTRUMENTS) {
    DBGLN("[InstrMgr] Max instruments reached!");
    return -1;
  }
  _instruments[_count] = cfg;
  _instruments[_count].id = nextId();
  _count++;
  _rebuildNoteMap();
  DBGF("[InstrMgr] Added instrument '%s' (id=%d, note=%d)\n",
       cfg.name, _instruments[_count - 1].id, cfg.midiNote);
  return _count - 1;
}

bool InstrumentManager::updateInstrument(uint8_t id, const InstrumentConfig& cfg) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_instruments[i].id == id) {
      uint8_t savedId = _instruments[i].id;
      _instruments[i] = cfg;
      _instruments[i].id = savedId; // Preserve ID
      _rebuildNoteMap();
      DBGF("[InstrMgr] Updated instrument '%s' (id=%d)\n", cfg.name, id);
      return true;
    }
  }
  return false;
}

bool InstrumentManager::removeInstrument(uint8_t id) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_instruments[i].id == id) {
      DBGF("[InstrMgr] Removed instrument '%s' (id=%d)\n", _instruments[i].name, id);
      // Shift remaining instruments
      for (uint8_t j = i; j < _count - 1; j++) {
        _instruments[j] = _instruments[j + 1];
      }
      _count--;
      _rebuildNoteMap();
      return true;
    }
  }
  return false;
}

InstrumentConfig* InstrumentManager::getInstrument(uint8_t id) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_instruments[i].id == id) return &_instruments[i];
  }
  return nullptr;
}

InstrumentConfig* InstrumentManager::getInstrumentByIndex(uint8_t index) {
  if (index < _count) return &_instruments[index];
  return nullptr;
}

// --- CRUD Actuators ---

int InstrumentManager::addActuator(uint8_t instrumentId, const ActuatorConfig& act) {
  InstrumentConfig* inst = getInstrument(instrumentId);
  if (!inst) return -1;
  if (inst->actuatorCount >= MAX_ACTUATORS_PER_INST) {
    DBGLN("[InstrMgr] Max actuators reached for instrument!");
    return -1;
  }
  inst->actuators[inst->actuatorCount] = act;
  inst->actuatorCount++;
  DBGF("[InstrMgr] Added actuator '%s' to '%s' (idx=%d)\n",
       act.name, inst->name, inst->actuatorCount - 1);
  return inst->actuatorCount - 1;
}

bool InstrumentManager::updateActuator(uint8_t instrumentId, uint8_t actIndex,
                                        const ActuatorConfig& act) {
  InstrumentConfig* inst = getInstrument(instrumentId);
  if (!inst || actIndex >= inst->actuatorCount) return false;
  inst->actuators[actIndex] = act;
  DBGF("[InstrMgr] Updated actuator '%s' in '%s'\n", act.name, inst->name);
  return true;
}

bool InstrumentManager::removeActuator(uint8_t instrumentId, uint8_t actIndex) {
  InstrumentConfig* inst = getInstrument(instrumentId);
  if (!inst || actIndex >= inst->actuatorCount) return false;
  DBGF("[InstrMgr] Removed actuator '%s' from '%s'\n",
       inst->actuators[actIndex].name, inst->name);
  // Shift remaining actuators
  for (uint8_t j = actIndex; j < inst->actuatorCount - 1; j++) {
    inst->actuators[j] = inst->actuators[j + 1];
  }
  inst->actuatorCount--;
  return true;
}

// --- MIDI dispatch ---

void InstrumentManager::onNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (velocity == 0) {
    onNoteOff(channel, note);
    return;
  }

  int8_t idx = _noteMap[note];
  if (idx < 0) return;

  InstrumentConfig& inst = _instruments[idx];
  if (!inst.enabled) return;

  // Check channel filter
  if (inst.midiChannel != 0 && inst.midiChannel != channel) return;

  DBGF("[MIDI] NoteOn ch=%d note=%d vel=%d -> '%s'\n", channel, note, velocity, inst.name);

  // Trigger PRE_STRIKE actuators first
  _triggerActuators(inst, TIMING_PRE_STRIKE, velocity);

  // Schedule ON_STRIKE actuators (with their individual delays)
  _triggerActuators(inst, TIMING_ON_STRIKE, velocity);

  // Schedule POST_STRIKE actuators
  _triggerActuators(inst, TIMING_POST_STRIKE, velocity);

  // Trigger CONTINUOUS actuators
  _triggerActuators(inst, TIMING_CONTINUOUS, velocity);
}

void InstrumentManager::onNoteOff(uint8_t channel, uint8_t note) {
  int8_t idx = _noteMap[note];
  if (idx < 0) return;

  InstrumentConfig& inst = _instruments[idx];
  if (!inst.enabled) return;
  if (inst.midiChannel != 0 && inst.midiChannel != channel) return;

  DBGF("[MIDI] NoteOff ch=%d note=%d -> '%s'\n", channel, note, inst.name);

  // Trigger ON_RELEASE actuators
  _triggerActuators(inst, TIMING_ON_RELEASE, 0, true);

  // Deactivate CONTINUOUS actuators
  for (uint8_t i = 0; i < inst.actuatorCount; i++) {
    if (inst.actuators[i].timing == TIMING_CONTINUOUS && inst.actuators[i].enabled) {
      _driver->deactivate(inst.actuators[i]);
    }
  }
}

void InstrumentManager::onControlChange(uint8_t channel, uint8_t cc, uint8_t value) {
  // Dispatch CC to actuators that listen to this CC number
  for (uint8_t i = 0; i < _count; i++) {
    InstrumentConfig& inst = _instruments[i];
    if (!inst.enabled) continue;
    if (inst.midiChannel != 0 && inst.midiChannel != channel) continue;

    for (uint8_t j = 0; j < inst.actuatorCount; j++) {
      ActuatorConfig& act = inst.actuators[j];
      if (act.enabled && act.control == CTRL_CC && act.ccNumber == cc) {
        _driver->activate(act, value);
      }
    }
  }
}

void InstrumentManager::onPitchBend(uint8_t channel, int16_t value) {
  // Map pitch bend (-8192 to +8191) to 0-127
  uint8_t mapped = map(value, -8192, 8191, 0, 127);

  for (uint8_t i = 0; i < _count; i++) {
    InstrumentConfig& inst = _instruments[i];
    if (!inst.enabled) continue;
    if (inst.midiChannel != 0 && inst.midiChannel != channel) continue;

    for (uint8_t j = 0; j < inst.actuatorCount; j++) {
      ActuatorConfig& act = inst.actuators[j];
      if (act.enabled && act.control == CTRL_PITCH_BEND) {
        _driver->activate(act, mapped);
      }
    }
  }
}

void InstrumentManager::onAftertouch(uint8_t channel, uint8_t pressure) {
  for (uint8_t i = 0; i < _count; i++) {
    InstrumentConfig& inst = _instruments[i];
    if (!inst.enabled) continue;
    if (inst.midiChannel != 0 && inst.midiChannel != channel) continue;

    for (uint8_t j = 0; j < inst.actuatorCount; j++) {
      ActuatorConfig& act = inst.actuators[j];
      if (act.enabled && act.control == CTRL_AFTERTOUCH) {
        _driver->activate(act, pressure);
      }
    }
  }
}

// --- Testing ---

void InstrumentManager::testInstrument(uint8_t id, uint8_t velocity) {
  InstrumentConfig* inst = getInstrument(id);
  if (!inst) return;
  DBGF("[InstrMgr] Testing instrument '%s' (vel=%d)\n", inst->name, velocity);
  onNoteOn(inst->midiChannel ? inst->midiChannel : 10, inst->midiNote, velocity);
}

void InstrumentManager::testActuator(uint8_t instrumentId, uint8_t actIndex, uint8_t value) {
  InstrumentConfig* inst = getInstrument(instrumentId);
  if (!inst || actIndex >= inst->actuatorCount) return;
  _driver->test(inst->actuators[actIndex], value);
}

// --- Serialization ---

void InstrumentManager::toJson(JsonArray& arr) const {
  for (uint8_t i = 0; i < _count; i++) {
    JsonObject obj = arr.add<JsonObject>();
    instrumentToJson(_instruments[i], obj);
  }
}

void InstrumentManager::instrumentToJson(const InstrumentConfig& inst, JsonObject& obj) const {
  obj["id"] = inst.id;
  obj["name"] = inst.name;
  obj["category"] = inst.category;
  obj["midiNote"] = inst.midiNote;
  obj["midiChannel"] = inst.midiChannel;
  obj["enabled"] = inst.enabled;

  JsonArray acts = obj["actuators"].to<JsonArray>();
  for (uint8_t j = 0; j < inst.actuatorCount; j++) {
    const ActuatorConfig& a = inst.actuators[j];
    JsonObject ao = acts.add<JsonObject>();
    ao["name"] = a.name;
    ao["type"] = (uint8_t)a.type;
    ao["timing"] = (uint8_t)a.timing;
    ao["control"] = (uint8_t)a.control;
    ao["bus"] = (uint8_t)a.bus;
    ao["hwAddress"] = a.hwAddress;
    ao["hwPin"] = a.hwPin;
    ao["paramMin"] = a.paramMin;
    ao["paramMax"] = a.paramMax;
    ao["paramDefault"] = a.paramDefault;
    ao["delayMs"] = a.delayMs;
    ao["ccNumber"] = a.ccNumber;
    ao["enabled"] = a.enabled;
  }
}

bool InstrumentManager::fromJson(const JsonArray& arr) {
  _count = 0;
  for (int i = 0; i < 128; i++) _noteMap[i] = -1;

  for (JsonObject obj : arr) {
    if (_count >= MAX_INSTRUMENTS) break;
    InstrumentConfig inst;
    if (instrumentFromJson(obj, inst)) {
      _instruments[_count] = inst;
      _count++;
    }
  }
  _rebuildNoteMap();
  DBGF("[InstrMgr] Loaded %d instruments from JSON\n", _count);
  return true;
}

bool InstrumentManager::instrumentFromJson(const JsonObject& obj, InstrumentConfig& inst) {
  inst.id = obj["id"] | 0;
  strlcpy(inst.name, obj["name"] | "Unnamed", sizeof(inst.name));
  strlcpy(inst.category, obj["category"] | "drums", sizeof(inst.category));
  inst.midiNote = obj["midiNote"] | 36;
  inst.midiChannel = obj["midiChannel"] | 10;
  inst.enabled = obj["enabled"] | true;

  inst.actuatorCount = 0;
  JsonArray acts = obj["actuators"].as<JsonArray>();
  if (!acts.isNull()) {
    for (JsonObject ao : acts) {
      if (inst.actuatorCount >= MAX_ACTUATORS_PER_INST) break;
      ActuatorConfig& a = inst.actuators[inst.actuatorCount];
      strlcpy(a.name, ao["name"] | "Actuator", sizeof(a.name));
      a.type = (ActuatorType)(ao["type"] | 0);
      a.timing = (ActuatorTiming)(ao["timing"] | 1);
      a.control = (ActuatorControl)(ao["control"] | 0);
      a.bus = (HardwareBus)(ao["bus"] | 0);
      a.hwAddress = ao["hwAddress"] | 0x20;
      a.hwPin = ao["hwPin"] | 0;
      a.paramMin = ao["paramMin"] | 8;
      a.paramMax = ao["paramMax"] | 30;
      a.paramDefault = ao["paramDefault"] | 15;
      a.delayMs = ao["delayMs"] | 0;
      a.ccNumber = ao["ccNumber"] | 0;
      a.enabled = ao["enabled"] | true;
      inst.actuatorCount++;
    }
  }
  return true;
}

// --- Lookup ---

InstrumentConfig* InstrumentManager::findByMidiNote(uint8_t note) {
  if (note >= 128) return nullptr;
  int8_t idx = _noteMap[note];
  if (idx < 0 || idx >= _count) return nullptr;
  return &_instruments[idx];
}

uint8_t InstrumentManager::nextId() const {
  uint8_t maxId = 0;
  for (uint8_t i = 0; i < _count; i++) {
    if (_instruments[i].id > maxId) maxId = _instruments[i].id;
  }
  return maxId + 1;
}

// --- Private ---

void InstrumentManager::_rebuildNoteMap() {
  for (int i = 0; i < 128; i++) _noteMap[i] = -1;
  for (uint8_t i = 0; i < _count; i++) {
    if (_instruments[i].enabled && _instruments[i].midiNote < 128) {
      _noteMap[_instruments[i].midiNote] = i;
    }
  }
}

void InstrumentManager::_triggerActuators(InstrumentConfig& inst, ActuatorTiming timing,
                                           uint8_t value, bool isNoteOff) {
  for (uint8_t i = 0; i < inst.actuatorCount; i++) {
    ActuatorConfig& act = inst.actuators[i];
    if (!act.enabled || act.timing != timing) continue;

    // Determine the control value
    uint8_t ctrlValue = value;
    // For CTRL_FIXED, use the default value
    if (act.control == CTRL_FIXED) {
      ctrlValue = act.paramDefault;
    }

    if (act.delayMs > 0 && !isNoteOff) {
      // Schedule delayed activation
      // We use the scheduler with a lambda-like callback
      // For simplicity, we directly activate after delay
      struct DelayedActivation {
        ActuatorDriver* driver;
        ActuatorConfig config;
        uint8_t value;
      };
      // Use static pool to avoid dynamic allocation
      static DelayedActivation pool[16];
      static uint8_t poolIdx = 0;
      pool[poolIdx] = {_driver, act, ctrlValue};
      uint8_t currentIdx = poolIdx;
      poolIdx = (poolIdx + 1) % 16;

      _scheduler->scheduleOnce(act.delayMs, [](void* data) {
        DelayedActivation* da = (DelayedActivation*)data;
        da->driver->activate(da->config, da->value);
      }, &pool[currentIdx]);
    } else {
      if (isNoteOff) {
        _driver->deactivate(act);
      } else {
        _driver->activate(act, ctrlValue);
      }
    }
  }
}
