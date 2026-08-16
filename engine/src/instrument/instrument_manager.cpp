#include "instrument_manager.h"
#include "instrument_validation.h"

InstrumentManager::InstrumentManager(ActuatorManager* actMgr, Scheduler* scheduler)
  : _actMgr(actMgr), _scheduler(scheduler), _count(0) {
  memset(_noteMap, -1, sizeof(_noteMap));
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
  DBGF("[InstrMgr] Added '%s' (id=%d, note=%d)\n",
       cfg.name, _instruments[_count - 1].id, cfg.midiNote);
  return _count - 1;
}

bool InstrumentManager::updateInstrument(uint8_t id, const InstrumentConfig& cfg) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_instruments[i].id == id) {
      uint8_t savedId = _instruments[i].id;
      _instruments[i] = cfg;
      _instruments[i].id = savedId;
      _rebuildNoteMap();
      DBGF("[InstrMgr] Updated '%s' (id=%d)\n", cfg.name, id);
      return true;
    }
  }
  return false;
}

bool InstrumentManager::removeInstrument(uint8_t id) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_instruments[i].id == id) {
      DBGF("[InstrMgr] Removed '%s' (id=%d)\n", _instruments[i].name, id);
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

// --- MIDI dispatch (mode legacy) ---

void InstrumentManager::onNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (velocity == 0) {
    onNoteOff(channel, note);
    return;
  }

  // The (channel, note) pair IS the key — the channel is no longer re-checked
  // after the fact against a note-only lookup.
  int8_t idx = _lookup(channel, note);
  if (idx < 0) return;

  InstrumentConfig& inst = _instruments[idx];

  DBGF("[MIDI] NoteOn ch=%d note=%d vel=%d -> '%s'\n", channel, note, velocity, inst.name);

  // Activer tous les actionneurs de l'instrument
  for (uint8_t i = 0; i < inst.actuatorCount; i++) {
    uint8_t actId = inst.actuatorIds[i];
    if (actId == 0xFF) continue;

    Actuator* act = _actMgr->getActuator(actId);
    if (!act || !act->isEnabled()) continue;

    // Generer la commande selon le type
    const ActuatorConfig& cfg = act->getConfig();
    uint32_t durationUs = map(velocity, 0, 127,
                              (uint32_t)cfg.paramMin * 1000,
                              (uint32_t)cfg.paramMax * 1000);
    _scheduler->schedulePulse(actId, velocity, durationUs);
  }
}

void InstrumentManager::onNoteOff(uint8_t channel, uint8_t note) {
  int8_t idx = _lookup(channel, note);
  if (idx < 0) return;

  InstrumentConfig& inst = _instruments[idx];

  // Envoyer OFF a tous les actionneurs
  for (uint8_t i = 0; i < inst.actuatorCount; i++) {
    uint8_t actId = inst.actuatorIds[i];
    if (actId == 0xFF) continue;

    ActuatorCommand cmd;
    cmd.actuator_id = actId;
    cmd.command_type = (uint8_t)CommandType::OFF;
    cmd.value = 0;
    cmd.duration = 0;
    cmd.execute_at = micros();
    _scheduler->scheduleCommand(cmd);
  }
}

void InstrumentManager::onControlChange(uint8_t channel, uint8_t cc, uint8_t value) {
  // Dispatch CC vers les actionneurs qui ecoutent ce CC
  for (uint8_t i = 0; i < _count; i++) {
    InstrumentConfig& inst = _instruments[i];
    if (!inst.enabled) continue;
    if (inst.midiChannel != 0 && inst.midiChannel != channel) continue;

    for (uint8_t j = 0; j < inst.actuatorCount; j++) {
      uint8_t actId = inst.actuatorIds[j];
      if (actId == 0xFF) continue;
      // En mode legacy, on dispatch le CC comme commande position
      ActuatorCommand cmd;
      cmd.actuator_id = actId;
      cmd.command_type = (uint8_t)CommandType::POSITION;
      cmd.value = value;
      cmd.duration = 0;
      cmd.execute_at = micros();
      _scheduler->scheduleCommand(cmd);
    }
  }
}

void InstrumentManager::onPitchBend(uint8_t channel, int16_t value) {
  uint8_t mapped = map(value, -8192, 8191, 0, 127);
  for (uint8_t i = 0; i < _count; i++) {
    InstrumentConfig& inst = _instruments[i];
    if (!inst.enabled) continue;
    if (inst.midiChannel != 0 && inst.midiChannel != channel) continue;

    for (uint8_t j = 0; j < inst.actuatorCount; j++) {
      uint8_t actId = inst.actuatorIds[j];
      if (actId == 0xFF) continue;
      Actuator* act = _actMgr->getActuator(actId);
      if (act && act->getConfig().type == ActuatorType::SERVO) {
        ActuatorCommand cmd;
        cmd.actuator_id = actId;
        cmd.command_type = (uint8_t)CommandType::POSITION;
        cmd.value = mapped;
        cmd.duration = 0;
        cmd.execute_at = micros();
        _scheduler->scheduleCommand(cmd);
      }
    }
  }
}

void InstrumentManager::onAftertouch(uint8_t channel, uint8_t pressure) {
  // Aftertouch dispatche comme modulation continue
  for (uint8_t i = 0; i < _count; i++) {
    InstrumentConfig& inst = _instruments[i];
    if (!inst.enabled) continue;
    if (inst.midiChannel != 0 && inst.midiChannel != channel) continue;

    for (uint8_t j = 0; j < inst.actuatorCount; j++) {
      uint8_t actId = inst.actuatorIds[j];
      if (actId == 0xFF) continue;
      ActuatorCommand cmd;
      cmd.actuator_id = actId;
      cmd.command_type = (uint8_t)CommandType::PWM;
      cmd.value = pressure;
      cmd.duration = 0;
      cmd.execute_at = micros();
      _scheduler->scheduleCommand(cmd);
    }
  }
}

// --- Testing ---

void InstrumentManager::testInstrument(uint8_t id, uint8_t velocity) {
  InstrumentConfig* inst = getInstrument(id);
  if (!inst) return;
  DBGF("[InstrMgr] Testing '%s' (vel=%d)\n", inst->name, velocity);
  onNoteOn(inst->midiChannel ? inst->midiChannel : 10, inst->midiNote, velocity);
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
  obj["pipelineId"] = inst.pipelineId;

  JsonArray actIds = obj["actuatorIds"].to<JsonArray>();
  for (uint8_t j = 0; j < inst.actuatorCount; j++) {
    actIds.add(inst.actuatorIds[j]);
  }

  obj["retriggerMode"] = inst.retriggerMode;

  JsonArray noteOn = obj["noteOnActions"].to<JsonArray>();
  for (uint8_t j = 0; j < inst.noteOnCount; j++) {
    const ActionStep& step = inst.noteOnActions[j];
    JsonObject s = noteOn.add<JsonObject>();
    s["actuatorId"] = step.actuator_id;
    s["commandType"] = step.command_type;
    s["valueSource"] = step.value_source;
    s["valueFixed"] = step.value_fixed;
    s["ccIndex"] = step.cc_index;
    s["delayMs"] = step.delay_ms;
    s["durationMinMs"] = step.duration_min_ms;
    s["durationMaxMs"] = step.duration_max_ms;
    s["velocityCurve"] = step.velocity_curve;
    if (step.alternate_group > 0) s["alternateGroup"] = step.alternate_group;
  }

  JsonArray noteOff = obj["noteOffActions"].to<JsonArray>();
  for (uint8_t j = 0; j < inst.noteOffCount; j++) {
    const ActionStep& step = inst.noteOffActions[j];
    JsonObject s = noteOff.add<JsonObject>();
    s["actuatorId"] = step.actuator_id;
    s["commandType"] = step.command_type;
    s["valueSource"] = step.value_source;
    s["valueFixed"] = step.value_fixed;
    s["ccIndex"] = step.cc_index;
    s["delayMs"] = step.delay_ms;
    s["durationMinMs"] = step.duration_min_ms;
    s["durationMaxMs"] = step.duration_max_ms;
    s["velocityCurve"] = step.velocity_curve;
    if (step.alternate_group > 0) s["alternateGroup"] = step.alternate_group;
  }

  JsonArray cc = obj["ccBindings"].to<JsonArray>();
  for (uint8_t j = 0; j < inst.ccBindingCount; j++) {
    const CCBinding& binding = inst.ccBindings[j];
    JsonObject b = cc.add<JsonObject>();
    b["ccNumber"] = binding.cc_number;
    b["actuatorId"] = binding.actuator_id;
    b["commandType"] = binding.command_type;
    b["rangeMin"] = binding.range_min;
    b["rangeMax"] = binding.range_max;
    b["curve"] = binding.curve;
    b["inverted"] = binding.inverted;
  }
}

bool InstrumentManager::fromJson(const JsonArray& arr) {
  _count = 0;
  memset(_noteMap, -1, sizeof(_noteMap));

  // review #14: validate each loaded instrument with the SAME validator the API
  // uses. Actuators are already created at this point, so resolve references via
  // the actuator manager. Invalid instruments (bad note/channel, dangling
  // actuator ref, incompatible command) are skipped rather than loaded.
  auto lookup = [](void* ctx, uint8_t id) -> const ActuatorConfig* {
    Actuator* a = ((ActuatorManager*)ctx)->getActuator(id);
    return a ? &a->getConfig() : nullptr;
  };

  for (JsonObject obj : arr) {
    if (_count >= MAX_INSTRUMENTS) break;
    InstrumentConfig inst;
    if (!instrumentFromJson(obj, inst)) continue;

    const char* err = nullptr;
    if (!validateInstrumentConfig(inst, err, lookup, _actMgr)) {
      DBGF("[InstrMgr] Skipped invalid instrument '%s': %s\n",
           inst.name, err ? err : "?");
      continue;
    }
    _instruments[_count] = inst;
    _count++;
  }
  _rebuildNoteMap();
  DBGF("[InstrMgr] Loaded %d instruments\n", _count);
  return true;
}

bool InstrumentManager::instrumentFromJson(const JsonObject& obj, InstrumentConfig& inst) {
  inst.id = obj["id"] | 0;
  strlcpy(inst.name, obj["name"] | "Unnamed", sizeof(inst.name));
  strlcpy(inst.category, obj["category"] | "drums", sizeof(inst.category));
  inst.midiNote = obj["midiNote"] | 36;
  inst.midiChannel = obj["midiChannel"] | 10;
  inst.enabled = obj["enabled"] | true;
  inst.pipelineId = obj["pipelineId"] | 0xFF;

  inst.actuatorCount = 0;
  JsonArray actIds = obj["actuatorIds"].as<JsonArray>();
  if (!actIds.isNull()) {
    for (JsonVariant v : actIds) {
      if (inst.actuatorCount >= MAX_ACTUATORS_PER_INST) break;
      inst.actuatorIds[inst.actuatorCount] = v.as<uint8_t>();
      inst.actuatorCount++;
    }
  }

  inst.retriggerMode = obj["retriggerMode"] | (uint8_t)RetriggerMode::IGNORE;

  inst.noteOnCount = 0;
  JsonArray noteOn = obj["noteOnActions"].as<JsonArray>();
  if (!noteOn.isNull()) {
    for (JsonObject stepObj : noteOn) {
      if (inst.noteOnCount >= MAX_ACTIONS_PER_EVENT) break;
      ActionStep& step = inst.noteOnActions[inst.noteOnCount++];
      step.actuator_id = stepObj["actuatorId"] | 0xFF;
      step.command_type = stepObj["commandType"] | (uint8_t)CommandType::PULSE;
      step.value_source = stepObj["valueSource"] | (uint8_t)ValueSource::VELOCITY;
      step.value_fixed = stepObj["valueFixed"] | 0;
      step.cc_index = stepObj["ccIndex"] | 0;
      step.delay_ms = stepObj["delayMs"] | 0;
      step.duration_min_ms = stepObj["durationMinMs"] | 0;
      step.duration_max_ms = stepObj["durationMaxMs"] | 0;
      step.velocity_curve = stepObj["velocityCurve"] | CURVE_LINEAR;
      step.alternate_group = stepObj["alternateGroup"] | 0;
    }
  }

  inst.noteOffCount = 0;
  JsonArray noteOff = obj["noteOffActions"].as<JsonArray>();
  if (!noteOff.isNull()) {
    for (JsonObject stepObj : noteOff) {
      if (inst.noteOffCount >= MAX_ACTIONS_PER_EVENT) break;
      ActionStep& step = inst.noteOffActions[inst.noteOffCount++];
      step.actuator_id = stepObj["actuatorId"] | 0xFF;
      step.command_type = stepObj["commandType"] | (uint8_t)CommandType::OFF;
      step.value_source = stepObj["valueSource"] | (uint8_t)ValueSource::FIXED;
      step.value_fixed = stepObj["valueFixed"] | 0;
      step.cc_index = stepObj["ccIndex"] | 0;
      step.delay_ms = stepObj["delayMs"] | 0;
      step.duration_min_ms = stepObj["durationMinMs"] | 0;
      step.duration_max_ms = stepObj["durationMaxMs"] | 0;
      step.velocity_curve = stepObj["velocityCurve"] | CURVE_LINEAR;
      step.alternate_group = stepObj["alternateGroup"] | 0;
    }
  }

  inst.ccBindingCount = 0;
  JsonArray cc = obj["ccBindings"].as<JsonArray>();
  if (!cc.isNull()) {
    for (JsonObject bindObj : cc) {
      if (inst.ccBindingCount >= MAX_CC_BINDINGS) break;
      CCBinding& binding = inst.ccBindings[inst.ccBindingCount++];
      binding.cc_number = bindObj["ccNumber"] | 0;
      binding.actuator_id = bindObj["actuatorId"] | 0xFF;
      binding.command_type = bindObj["commandType"] | (uint8_t)CommandType::POSITION;
      binding.range_min = bindObj["rangeMin"] | 0;
      binding.range_max = bindObj["rangeMax"] | 127;
      binding.curve = bindObj["curve"] | CURVE_LINEAR;
      binding.inverted = bindObj["inverted"] | false;
    }
  }

  return true;
}

// --- Lookup ---

InstrumentConfig* InstrumentManager::findByMidiNote(uint8_t channel, uint8_t note) {
  int8_t idx = _lookup(channel, note);
  if (idx < 0) return nullptr;
  return &_instruments[idx];
}

int8_t InstrumentManager::_lookup(uint8_t channel, uint8_t note) const {
  if (note >= 128) return -1;
  if (channel < 1 || channel > MIDI_CHANNEL_COUNT) return -1;
  int8_t idx = _noteMap[channel - 1][note];
  if (idx < 0 || idx >= (int8_t)_count) return -1;
  if (!_instruments[idx].enabled) return -1;
  return idx;
}

uint8_t InstrumentManager::nextId() const {
  uint8_t maxId = 0;
  for (uint8_t i = 0; i < _count; i++) {
    if (_instruments[i].id > maxId) maxId = _instruments[i].id;
  }
  return maxId + 1;
}

// Precedence rules shared with PipelineCompiler::buildNoteMap() — see
// core/midi_routing.h.
void InstrumentManager::_rebuildNoteMap() {
  uint16_t shadowed = midiBuildNoteMap<int8_t>(
      _noteMap, -1, _count,
      [this](uint16_t i) -> MidiBinding {
        const InstrumentConfig& inst = _instruments[i];
        return { inst.midiChannel, inst.midiNote,
                 inst.enabled && inst.midiNote < 128 &&
                 inst.midiChannel <= MIDI_CHANNEL_COUNT };
      });

  if (shadowed > 0) {
    DBGF("[InstrMgr] %u instrument(s) shadowed by an earlier (channel, note) mapping\n",
         shadowed);
  }
}
