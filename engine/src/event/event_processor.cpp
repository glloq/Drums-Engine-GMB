#include "event_processor.h"
#include <math.h>

EventProcessor::EventProcessor(Scheduler* scheduler, EngineState* state)
  : _scheduler(scheduler), _state(state) {}

void EventProcessor::processMidiEvent(const MidiEvent& ev) {
  if (ev.type == MIDI_EVT_NOTE_ON && ev.data2 > 0) {
    uint8_t pipelineIdx = _lookup.note_to_pipeline[ev.data1];
    if (pipelineIdx == 0xFF || pipelineIdx >= _lookup.pipeline_count) return;

    const CompiledPipeline& pipeline = _lookup.pipelines[pipelineIdx];
    if (pipeline.note_on_count > 0) {
      _scheduler->scheduleActionSteps(pipeline.note_on_actions, pipeline.note_on_count,
                                      ev.data2, ev.timestamp, _state->raw().variables);
      return;
    }

    _executePipeline(pipelineIdx, ev.data2, ev.timestamp);
  }
  else if (ev.type == MIDI_EVT_NOTE_OFF || (ev.type == MIDI_EVT_NOTE_ON && ev.data2 == 0)) {
    uint8_t pipelineIdx = _lookup.note_to_pipeline[ev.data1];
    if (pipelineIdx == 0xFF || pipelineIdx >= _lookup.pipeline_count) return;

    const CompiledPipeline& pipeline = _lookup.pipelines[pipelineIdx];
    if (pipeline.note_off_count > 0) {
      _scheduler->scheduleActionSteps(pipeline.note_off_actions, pipeline.note_off_count,
                                      0, ev.timestamp, _state->raw().variables);
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
  }
  else if (ev.type == MIDI_EVT_CC) {
    _processCcEvent(ev);
  }
}


void EventProcessor::_processCcEvent(const MidiEvent& ev) {
  if (ev.data1 < MAX_GLOBAL_VARS) {
    _state->setVariable(ev.data1, ev.data2);
  }

  for (uint8_t i = 0; i < _lookup.pipeline_count; i++) {
    const CompiledPipeline& pipeline = _lookup.pipelines[i];
    for (uint8_t j = 0; j < pipeline.cc_binding_count; j++) {
      const CCBinding& binding = pipeline.cc_bindings[j];
      if (binding.cc_number != ev.data1 || binding.actuator_id == 0xFF) continue;

      uint8_t inVal = binding.inverted ? (127 - ev.data2) : ev.data2;
      uint8_t curved = _applyVelocityCurve(inVal, binding.curve);
      uint16_t mapped = map(curved, 0, 127, binding.range_min, binding.range_max);

      ActuatorCommand cmd;
      cmd.actuator_id = binding.actuator_id;
      cmd.command_type = binding.command_type;
      cmd.value = mapped;
      cmd.duration = 0;
      cmd.execute_at = ev.timestamp;
      _scheduler->scheduleCommand(cmd);
    }
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
        if (result < 0) return; // Pipeline arrete par condition
        currentValue = (uint8_t)result;
        break;
      }

      case BlockType::TRANSFORM: {
        currentValue = _processTransformBlock(block, currentValue);
        break;
      }

      case BlockType::TIME: {
        if (block.subtype == TIME_DELAY) {
          // Delai simple: accumuler
          delayAccum += (uint32_t)block.param2 * 1000; // ms -> us
        }
        else if (block.subtype == TIME_PULSE) {
          // Generer impulsion via scheduler
          _processTimePulse(block, pipeline.output_actuator_id,
                           currentValue, timestamp, delayAccum);
          return; // L'impulsion genere ON+OFF, pipeline termine
        }
        else if (block.subtype == TIME_RAMP) {
          // Generer rampe de micro-steps pour servo/moteur
          _processTimeRamp(block, pipeline.output_actuator_id,
                          currentValue, timestamp, delayAccum);
          return; // Le ramp genere N commandes, pipeline termine
        }
        break;
      }

      case BlockType::OUTPUT: {
        _processOutputBlock(block, currentValue, timestamp, delayAccum);
        return;
      }
    }
  }

  // Si aucun bloc OUTPUT explicite, generer une commande par defaut
  if (pipeline.output_actuator_id != 0xFF) {
    ActuatorCommand cmd;
    cmd.actuator_id = pipeline.output_actuator_id;
    cmd.command_type = (uint8_t)CommandType::PULSE;
    cmd.value = currentValue;
    cmd.duration = 0;
    cmd.execute_at = timestamp + delayAccum;
    _scheduler->scheduleCommand(cmd);
  }
}

// --- CONDITION blocks ---

int16_t EventProcessor::_processConditionBlock(const CompiledBlock& block, uint8_t value,
                                                 uint8_t pipelineIdx) {
  switch (block.subtype) {
    case COND_THRESHOLD: {
      // param1 bits[6:0] = variable index (0x7F = use velocity)
      // param1 bit[7] = direction (0: >=, 1: <)
      uint8_t varIdx = block.param1 & 0x7F;
      bool isLessThan = (block.param1 & 0x80) != 0;

      uint16_t sourceVal;
      if (varIdx == 0x7F) {
        sourceVal = value; // Utiliser la velocite
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
      // param1: index de sortie que ce pipeline gere
      // param2: nombre total de sorties
      uint8_t numOutputs = block.param2 & 0xFF;
      uint8_t current = _state->advanceRoundRobin(pipelineIdx, numOutputs);
      if (current != block.param1) return -1;
      return value;
    }

    case COND_VELOCITY_SPLIT: {
      // param1: velocity min, param2: velocity max
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

// --- TRANSFORM blocks ---

uint8_t EventProcessor::_processTransformBlock(const CompiledBlock& block, uint8_t value) {
  switch (block.subtype) {
    case TRANS_VELOCITY_CURVE:
      return _applyVelocityCurve(value, block.param1);

    case TRANS_GAIN: {
      // param2: gain * 100 (ex: 80 = 0.80, 150 = 1.50)
      uint16_t result = ((uint16_t)value * block.param2) / 100;
      return (result > 127) ? 127 : (uint8_t)result;
    }

    case TRANS_CLAMP: {
      // param1: min, param2: max
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

// --- TIME blocks ---

uint32_t EventProcessor::_processTimePulse(const CompiledBlock& block, uint8_t actuatorId,
                                            uint8_t value, uint32_t timestamp, uint32_t delayAccum) {
  // param1: min duration ms (if > 0, velocity-dependent)
  // param2: max duration ms
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
  // Ramping: generer des micro-steps pour transition progressive
  // param1: nombre de steps (0 = auto ~5ms par step)
  // param2: duree totale en ms
  //
  // Genere N commandes POSITION de 0 -> value sur la duree totale
  // Ex: position 10->50 en 20ms = 4 steps de 5ms
  //     = 4 commandes POSITION schedulees a t+0, t+5ms, t+10ms, t+15ms

  uint32_t totalDurationUs = (uint32_t)block.param2 * 1000;
  uint8_t stepCount = block.param1;
  if (stepCount == 0) {
    stepCount = totalDurationUs / 5000; // ~5ms per step
    if (stepCount < 2) stepCount = 2;
    if (stepCount > 20) stepCount = 20; // Limiter la queue
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

    if (!_scheduler->scheduleCommand(cmd)) break; // Queue pleine
  }
}

// --- OUTPUT blocks ---

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

// --- Velocity curves ---

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
  DBGF("[EventProc] Lookup recompiled (%d pipelines)\n", _lookup.pipeline_count);
}
