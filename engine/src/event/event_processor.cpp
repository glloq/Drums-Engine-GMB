#include "event_processor.h"
#include <math.h>

EventProcessor::EventProcessor(Scheduler* scheduler, EngineState* state)
  : _scheduler(scheduler), _state(state) {
  memset(_lastCcDispatchUs, 0, sizeof(_lastCcDispatchUs));
  memset(_noteActive, 0, sizeof(_noteActive));
  _ccStats = {};
}

void EventProcessor::processMidiEvent(const MidiEvent& ev) {
  if (ev.type == MIDI_EVT_NOTE_ON && ev.data2 > 0) {
    uint8_t pipelineIdx = _lookup.note_to_pipeline[ev.data1];
    if (pipelineIdx == 0xFF || pipelineIdx >= _lookup.pipeline_count) return;

    const CompiledPipeline& pipeline = _lookup.pipelines[pipelineIdx];

    if (_noteActive[ev.data1]) {
      if (pipeline.retrigger_mode == (uint8_t)RetriggerMode::IGNORE) {
        return;
      }
      if (pipeline.retrigger_mode == (uint8_t)RetriggerMode::RESET && pipeline.note_off_count > 0) {
        _scheduler->scheduleActionSteps(pipeline.note_off_actions, pipeline.note_off_count,
                                        ev.data2, ev.timestamp, _state->raw().variables);
      }
    }

    if (pipeline.note_on_count > 0) {
      _scheduler->scheduleActionSteps(pipeline.note_on_actions, pipeline.note_on_count,
                                      ev.data2, ev.timestamp, _state->raw().variables);
      _noteActive[ev.data1] = true;
      return;
    }

    _executePipeline(pipelineIdx, ev.data2, ev.timestamp);
    _noteActive[ev.data1] = true;
  }
  else if (ev.type == MIDI_EVT_NOTE_OFF || (ev.type == MIDI_EVT_NOTE_ON && ev.data2 == 0)) {
    uint8_t pipelineIdx = _lookup.note_to_pipeline[ev.data1];
    if (pipelineIdx == 0xFF || pipelineIdx >= _lookup.pipeline_count) return;

    const CompiledPipeline& pipeline = _lookup.pipelines[pipelineIdx];
    if (pipeline.note_off_count > 0) {
      _scheduler->scheduleActionSteps(pipeline.note_off_actions, pipeline.note_off_count,
                                      ev.data2, ev.timestamp, _state->raw().variables);
      _noteActive[ev.data1] = false;
      return;
    }

    if (pipeline.output_actuator_id != 0xFF) {
      ActuatorCommand cmd;
      cmd.actuator_id = pipeline.output_actuator_id;
      cmd.command_type = (uint8_t)CommandType::OFF;
      cmd.value = 0;
      cmd.duration = 0;
      cmd.execute_at = ev.timestamp;
      _scheduler->scheduleCommand(cmd);
    }
    _noteActive[ev.data1] = false;
  }
  else if (ev.type == MIDI_EVT_CC) {
    _processCcEvent(ev);
  }
  else if (ev.type == MIDI_EVT_PITCH_BEND) {
    // Treat pitch bend as virtual CC127 for pitch-oriented routes
    uint16_t bend14 = ((uint16_t)ev.data1 << 7) | (uint16_t)ev.data2;
    uint8_t mapped = map((int)bend14, 0, 16383, 0, 127);
    MidiEvent ccEv = ev;
    ccEv.type = MIDI_EVT_CC;
    ccEv.data1 = 127;
    ccEv.data2 = mapped;
    _processCcEvent(ccEv);
  }
}

void EventProcessor::_processCcEvent(const MidiEvent& ev) {
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

void EventProcessor::_executePipeline(uint8_t pipelineIdx, uint8_t value, uint32_t timestamp) {
  if (pipelineIdx >= _lookup.pipeline_count) return;

  const CompiledPipeline& pipeline = _lookup.pipelines[pipelineIdx];
  uint8_t currentValue = value;
  uint32_t delayAccum = 0;

  for (uint8_t i = 0; i < pipeline.block_count; i++) {
    const CompiledBlock& block = pipeline.blocks[i];

    switch ((BlockType)block.type) {
      case BlockType::CONDITION: {
        int16_t result = _processConditionBlock(block, currentValue, pipelineIdx);
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
          delayAccum += (uint32_t)block.param2 * 1000;
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
                                               uint8_t pipelineIdx) {
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
