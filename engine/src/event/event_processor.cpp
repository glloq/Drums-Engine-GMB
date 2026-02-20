#include "event_processor.h"
#include <math.h>

// Spinlock for _noteActive[] shared between Core 0 (LoopEngine) and Core 1 (MIDI RT)
static portMUX_TYPE _noteActiveMux = portMUX_INITIALIZER_UNLOCKED;

EventProcessor::EventProcessor(Scheduler* scheduler, EngineState* state)
  : _scheduler(scheduler), _state(state) {
  memset(_lastCcDispatchUs, 0, sizeof(_lastCcDispatchUs));
  memset(_noteActive, 0, sizeof(_noteActive));
  _ccStats = {};
}

void EventProcessor::getNoteActive(uint8_t* out) const {
  portENTER_CRITICAL(&_noteActiveMux);
  memcpy(out, _noteActive, 128);
  portEXIT_CRITICAL(&_noteActiveMux);
}

void EventProcessor::processMidiEvent(const MidiEvent& ev) {
  if (ev.type == MIDI_EVT_NOTE_ON && ev.data2 > 0) {
    uint8_t pipelineIdx = _lookup.note_to_pipeline[ev.data1];
    if (pipelineIdx == 0xFF || pipelineIdx >= _lookup.pipeline_count) return;

    const CompiledPipeline& pipeline = _lookup.pipelines[pipelineIdx];

    portENTER_CRITICAL(&_noteActiveMux);
    uint8_t noteCount = _noteActive[ev.data1];
    portEXIT_CRITICAL(&_noteActiveMux);

    if (noteCount > 0) {
      if (pipeline.retrigger_mode == (uint8_t)RetriggerMode::IGNORE) {
        return;
      }
      if (pipeline.retrigger_mode == (uint8_t)RetriggerMode::RESET && pipeline.note_off_count > 0) {
        _scheduler->scheduleActionSteps(pipeline.note_off_actions, pipeline.note_off_count,
                                        ev.data2, ev.timestamp, _state->raw().variables);
        portENTER_CRITICAL(&_noteActiveMux);
        _noteActive[ev.data1] = 0;  // Reset counter before re-triggering
        portEXIT_CRITICAL(&_noteActiveMux);
      }
      // STACK: counter will increment below, allowing proper NoteOff tracking
    }

    if (pipeline.note_on_count > 0) {
      uint8_t activeGroup = 0;
      if (pipeline.alternate_group_count > 0) {
        // Round-robin: advance counter, get active group (1-based)
        uint8_t rr = _state->advanceRoundRobin(pipelineIdx, pipeline.alternate_group_count);
        activeGroup = rr + 1; // advanceRoundRobin returns 0-based, groups are 1-based
      }
      _scheduler->scheduleActionSteps(pipeline.note_on_actions, pipeline.note_on_count,
                                      ev.data2, ev.timestamp, _state->raw().variables,
                                      activeGroup);
      // Push LED event (Core 1 → Core 0, lock-free)
      if (_ledQueue) {
        LedEvent ledEv;
        ledEv.midiNote = ev.data1;
        ledEv.velocity = ev.data2;
        ledEv.noteOn = true;
        ledEv.segmentId = 0xFF; // Broadcast: LedEngine will resolve via note lookup
        _ledQueue->push(ledEv);
      }
      portENTER_CRITICAL(&_noteActiveMux);
      if (_noteActive[ev.data1] < 255) _noteActive[ev.data1]++;
      portEXIT_CRITICAL(&_noteActiveMux);
      return;
    }

    _executePipeline(pipelineIdx, ev.data2, ev.timestamp, ev.data1);
    // Push LED event for pipeline-path notes too
    if (_ledQueue) {
      LedEvent ledEv;
      ledEv.midiNote = ev.data1;
      ledEv.velocity = ev.data2;
      ledEv.noteOn = true;
      ledEv.segmentId = 0xFF;
      _ledQueue->push(ledEv);
    }
    portENTER_CRITICAL(&_noteActiveMux);
    if (_noteActive[ev.data1] < 255) _noteActive[ev.data1]++;
    portEXIT_CRITICAL(&_noteActiveMux);
  }
  else if (ev.type == MIDI_EVT_NOTE_OFF || (ev.type == MIDI_EVT_NOTE_ON && ev.data2 == 0)) {
    uint8_t pipelineIdx = _lookup.note_to_pipeline[ev.data1];
    if (pipelineIdx == 0xFF || pipelineIdx >= _lookup.pipeline_count) return;

    const CompiledPipeline& pipeline = _lookup.pipelines[pipelineIdx];

    // C3 fix: Atomic read-check-modify to prevent race between cores
    bool fireNoteOff = false;
    portENTER_CRITICAL(&_noteActiveMux);
    uint8_t noteOffCount = _noteActive[ev.data1];
    if (noteOffCount == 0) {
      portEXIT_CRITICAL(&_noteActiveMux);
      return;  // No active note — ignore spurious NoteOff
    }
    if (noteOffCount > 1 &&
        pipeline.retrigger_mode == (uint8_t)RetriggerMode::STACK) {
      _noteActive[ev.data1]--;
      portEXIT_CRITICAL(&_noteActiveMux);
      return;  // Other instances still active, don't fire noteOff yet
    }
    // Last instance releasing — zero counter and fire noteOff
    _noteActive[ev.data1] = 0;
    fireNoteOff = true;
    portEXIT_CRITICAL(&_noteActiveMux);

    if (fireNoteOff) {
      if (pipeline.note_off_count > 0) {
        _scheduler->scheduleActionSteps(pipeline.note_off_actions, pipeline.note_off_count,
                                        ev.data2, ev.timestamp, _state->raw().variables);
      } else if (pipeline.output_actuator_id != 0xFF) {
        ActuatorCommand cmd;
        cmd.actuator_id = pipeline.output_actuator_id;
        cmd.command_type = (uint8_t)CommandType::OFF;
        cmd.value = 0;
        cmd.duration = 0;
        cmd.execute_at = ev.timestamp;
        _scheduler->scheduleCommand(cmd);
      }

      // Push LED NoteOff event (fade out animation)
      if (_ledQueue) {
        LedEvent ledEv;
        ledEv.midiNote = ev.data1;
        ledEv.velocity = ev.data2;
        ledEv.noteOn = false;
        ledEv.segmentId = 0xFF;
        _ledQueue->push(ledEv);
      }
    }
  }
  else if (ev.type == MIDI_EVT_CC) {
    _processCcEvent(ev);
  }
  else if (ev.type == MIDI_EVT_PITCH_BEND) {
    // Route pitch bend to all actuators with PITCH_BEND behavior via CC routes
    // Uses virtual CC#126 (reserved) to avoid collision with real CC#127 (Poly Mode On)
    uint16_t bend14 = ((uint16_t)ev.data1 << 7) | (uint16_t)ev.data2;
    uint8_t mapped = map((int)bend14, 0, 16383, 0, 127);
    MidiEvent ccEv = ev;
    ccEv.type = MIDI_EVT_CC;
    ccEv.data1 = VIRTUAL_CC_PITCH_BEND;
    ccEv.data2 = mapped;
    _processCcEvent(ccEv);
  }
  else if (ev.type == MIDI_EVT_AFTERTOUCH) {
    // Route channel aftertouch via virtual CC#125 — same anti-flood and routing as CC
    MidiEvent ccEv = ev;
    ccEv.type = MIDI_EVT_CC;
    ccEv.data1 = VIRTUAL_CC_AFTERTOUCH;
    ccEv.data2 = ev.data1;  // aftertouch: data1 = pressure, data2 unused
    _processCcEvent(ccEv);
  }
  else if (ev.type == MIDI_EVT_POLY_AFTERTOUCH) {
    // Poly aftertouch: data1 = note, data2 = pressure
    // Route via same virtual CC#125 — pressure value drives the CC route
    MidiEvent ccEv = ev;
    ccEv.type = MIDI_EVT_CC;
    ccEv.data1 = VIRTUAL_CC_AFTERTOUCH;
    ccEv.data2 = ev.data2;  // poly aftertouch: data2 = pressure
    _processCcEvent(ccEv);
  }
}

void EventProcessor::_processCcEvent(const MidiEvent& ev) {
  if (ev.data1 >= 128) return;  // Guard against out-of-bounds CC number
  _ccStats.received++;
  if (ev.data1 < MAX_GLOBAL_VARS) {
    _state->setVariable(ev.data1, ev.data2);
  }

  // Anti-flood: max 1 dispatch batch per CC every 5ms
  uint32_t lastTs = _lastCcDispatchUs[ev.data1];
  if (lastTs != 0 && (ev.timestamp - lastTs) < 5000) {
    _ccStats.throttled++;
    return;
  }
  _lastCcDispatchUs[ev.data1] = ev.timestamp;

  uint8_t first = _lookup.cc_to_first[ev.data1];
  uint8_t count = _lookup.cc_to_count[ev.data1];
  if (first == 0xFF || count == 0) {
    _ccStats.unrouted++;
    return;
  }

  _ccStats.routed_batches++;
  for (uint8_t i = 0; i < count; i++) {
    uint8_t routeIdx = first + i;
    if (routeIdx >= _lookup.cc_route_count) break;

    const CCRoutingEntry& route = _lookup.cc_routes[routeIdx];
    if (route.actuator_id == 0xFF) continue;

    uint8_t inVal = route.inverted ? (127 - ev.data2) : ev.data2;
    uint8_t curved = _applyVelocityCurve(inVal, route.curve);
    uint16_t mapped = map(curved, 0, 127, route.range_min, route.range_max);

    ActuatorCommand cmd;
    cmd.actuator_id = route.actuator_id;
    cmd.command_type = route.command_type;
    cmd.value = mapped;
    cmd.duration = 0;
    cmd.execute_at = ev.timestamp;
    _scheduler->scheduleCommand(cmd);
    _ccStats.routed_commands++;
  }
}

void EventProcessor::_executePipeline(uint8_t pipelineIdx, uint8_t value, uint32_t timestamp, uint8_t midiNote) {
  if (pipelineIdx >= _lookup.pipeline_count) return;

  const CompiledPipeline& pipeline = _lookup.pipelines[pipelineIdx];
  uint8_t currentValue = value;
  uint32_t delayAccum = 0;

  for (uint8_t i = 0; i < pipeline.block_count; i++) {
    const CompiledBlock& block = pipeline.blocks[i];

    switch ((BlockType)block.type) {
      case BlockType::CONDITION: {
        int16_t result = _processConditionBlock(block, currentValue, pipelineIdx, midiNote);
        if (result < 0) return;
        currentValue = (uint8_t)result;
        break;
      }

      case BlockType::TRANSFORM: {
        currentValue = _processTransformBlock(block, currentValue);
        break;
      }

      case BlockType::TIME: {
        if (block.subtype == TIME_DELAY) {
          uint32_t addUs = (uint32_t)block.param2 * 1000;
          if (delayAccum + addUs < delayAccum) {
            delayAccum = UINT32_MAX;  // Saturate on overflow
          } else {
            delayAccum += addUs;
          }
        }
        else if (block.subtype == TIME_PULSE) {
          _processTimePulse(block, pipeline.output_actuator_id,
                           currentValue, timestamp, delayAccum);
          return;
        }
        else if (block.subtype == TIME_RAMP) {
          _processTimeRamp(block, pipeline.output_actuator_id,
                          currentValue, timestamp, delayAccum);
          return;
        }
        else if (block.subtype == TIME_GATE) {
          // Minimum time between events (param2 = ms)
          // If last activation was too recent, halt pipeline
          uint32_t minIntervalUs = (uint32_t)block.param2 * 1000;
          uint32_t lastTs = _state->raw().gate_timestamps[pipelineIdx];
          if (lastTs != 0 && (timestamp - lastTs) < minIntervalUs) {
            return; // Too soon, reject
          }
          _state->raw().gate_timestamps[pipelineIdx] = timestamp;
        }
        break;
      }

      case BlockType::OUTPUT_BLOCK: {
        _processOutputBlock(block, currentValue, timestamp, delayAccum);
        return;
      }
    }
  }

  if (pipeline.output_actuator_id != 0xFF) {
    ActuatorCommand cmd;
    cmd.actuator_id = pipeline.output_actuator_id;
    cmd.command_type = (uint8_t)CommandType::PULSE;
    cmd.value = currentValue;
    cmd.duration = 0;
    cmd.execute_at = timestamp + delayAccum;
    _scheduler->scheduleCommand(cmd);
    _ccStats.routed_commands++;
  }
}

int16_t EventProcessor::_processConditionBlock(const CompiledBlock& block, uint8_t value,
                                               uint8_t pipelineIdx, uint8_t midiNote) {
  switch (block.subtype) {
    case COND_THRESHOLD: {
      uint8_t varIdx = block.param1 & 0x7F;
      bool isLessThan = (block.param1 & 0x80) != 0;

      uint16_t sourceVal;
      if (varIdx == 0x7F) {
        sourceVal = value;
      } else {
        sourceVal = _state->getVariable(varIdx);
      }

      if (isLessThan) {
        return (sourceVal < block.param2) ? value : -1;
      } else {
        return (sourceVal >= block.param2) ? value : -1;
      }
    }

    case COND_ROUND_ROBIN: {
      uint8_t numOutputs = block.param2 & 0xFF;
      uint8_t current = _state->advanceRoundRobin(pipelineIdx, numOutputs);
      if (current != block.param1) return -1;
      return value;
    }

    case COND_VELOCITY_SPLIT: {
      uint8_t vMin = block.param1;
      uint8_t vMax = block.param2 & 0xFF;
      if (value >= vMin && value <= vMax) {
        return value;
      }
      return -1;
    }

    case COND_RANDOM: {
      // param2 = probability percentage (0-100)
      // XOR micros with value and pipelineIdx for better distribution
      uint8_t chance = block.param2 & 0xFF;
      uint32_t seed = micros();
      seed ^= (seed >> 16);
      seed ^= ((uint32_t)value << 8) ^ ((uint32_t)pipelineIdx << 4);
      uint8_t roll = (uint8_t)(seed & 0xFF) % 100;
      return (roll < chance) ? value : -1;
    }

    case COND_NOTE_RANGE: {
      // param1 = note min, param2 = note max — filter by MIDI note number
      uint8_t noteMin = block.param1;
      uint8_t noteMax = block.param2 & 0xFF;
      return (midiNote >= noteMin && midiNote <= noteMax) ? value : -1;
    }

    default:
      return value;
  }
}

uint8_t EventProcessor::_processTransformBlock(const CompiledBlock& block, uint8_t value) {
  switch (block.subtype) {
    case TRANS_VELOCITY_CURVE:
      return _applyVelocityCurve(value, block.param1);

    case TRANS_GAIN: {
      uint16_t result = ((uint16_t)value * block.param2) / 100;
      return (result > 127) ? 127 : (uint8_t)result;
    }

    case TRANS_CLAMP: {
      uint8_t cMin = block.param1;
      uint8_t cMax = block.param2 & 0xFF;
      if (value < cMin) return cMin;
      if (value > cMax) return cMax;
      return value;
    }

    case TRANS_INVERT:
      return 127 - value;

    case TRANS_SET_VAR: {
      // Store current value in a global variable (param1 = variable index)
      // Value passes through unchanged — side effect only
      uint8_t varIdx = block.param1;
      if (varIdx < MAX_GLOBAL_VARS) {
        _state->setVariable(varIdx, value);
      }
      return value;
    }

    default:
      return value;
  }
}

uint32_t EventProcessor::_processTimePulse(const CompiledBlock& block, uint8_t actuatorId,
                                           uint8_t value, uint32_t timestamp, uint32_t delayAccum) {
  uint32_t durationUs;
  if (block.param1 > 0) {
    durationUs = map(value, 0, 127,
                     (uint32_t)block.param1 * 1000,
                     (uint32_t)block.param2 * 1000);
  } else {
    durationUs = (uint32_t)block.param2 * 1000;
  }

  _scheduler->schedulePulseAt(actuatorId, value, durationUs,
                              timestamp + delayAccum);
  return durationUs;
}

void EventProcessor::_processTimeRamp(const CompiledBlock& block, uint8_t actuatorId,
                                      uint8_t value, uint32_t timestamp, uint32_t delayAccum) {
  uint32_t totalDurationUs = (uint32_t)block.param2 * 1000;
  if (totalDurationUs == 0) return;  // No duration = no ramp
  uint8_t stepCount = block.param1;
  if (stepCount == 0) {
    stepCount = totalDurationUs / 5000;
    if (stepCount < 2) stepCount = 2;
    if (stepCount > 20) stepCount = 20;
  }

  uint32_t stepInterval = totalDurationUs / stepCount;
  uint32_t baseTime = timestamp + delayAccum;

  for (uint8_t i = 0; i < stepCount; i++) {
    uint8_t stepVal = (uint8_t)(((uint16_t)value * (i + 1)) / stepCount);

    ActuatorCommand cmd;
    cmd.actuator_id = actuatorId;
    cmd.command_type = (uint8_t)CommandType::POSITION;
    cmd.value = stepVal;
    cmd.duration = 0;
    cmd.execute_at = baseTime + (stepInterval * i);

    if (!_scheduler->scheduleCommand(cmd)) break;
  }
}

void EventProcessor::_processOutputBlock(const CompiledBlock& block, uint8_t value,
                                         uint32_t timestamp, uint32_t delayAccum) {
  ActuatorCommand cmd;
  cmd.actuator_id = block.param1;
  cmd.value = value;
  cmd.execute_at = timestamp + delayAccum;

  switch (block.subtype) {
    case OUT_PULSE:
      cmd.command_type = (uint8_t)CommandType::PULSE;
      cmd.duration = block.param2;
      break;

    case OUT_POSITION:
      cmd.command_type = (uint8_t)CommandType::POSITION;
      cmd.duration = 0;
      break;

    case OUT_PWM:
      cmd.command_type = (uint8_t)CommandType::PWM;
      cmd.duration = 0;
      break;

    case OUT_OFF:
      cmd.command_type = (uint8_t)CommandType::OFF;
      cmd.value = 0;
      cmd.duration = 0;
      break;

    default:
      cmd.command_type = (uint8_t)CommandType::PULSE;
      cmd.duration = 0;
      break;
  }

  _scheduler->scheduleCommand(cmd);
}

uint8_t EventProcessor::_applyVelocityCurve(uint8_t value, uint8_t curveType) {
  float normalized = value / 127.0f;
  float result;

  switch (curveType) {
    case CURVE_LINEAR:
      return value;

    case CURVE_EXPO:
      result = normalized * normalized;
      return (uint8_t)(result * 127.0f);

    case CURVE_LOG:
      result = sqrtf(normalized);
      return (uint8_t)(result * 127.0f);

    default:
      return value;
  }
}

void EventProcessor::recompileLookup() {
  memset(_lookup.note_to_pipeline, 0xFF, sizeof(_lookup.note_to_pipeline));
  _lookup.cc_route_count = 0;
  _lookup.cc_route_dropped = 0;
  memset(_lookup.cc_to_first, 0xFF, sizeof(_lookup.cc_to_first));
  memset(_lookup.cc_to_count, 0, sizeof(_lookup.cc_to_count));
  memset(_lastCcDispatchUs, 0, sizeof(_lastCcDispatchUs));
  memset(_noteActive, 0, sizeof(_noteActive));
  _ccStats = {};
  DBGF("[EventProc] Lookup recompiled (%d pipelines)\n", _lookup.pipeline_count);
}
