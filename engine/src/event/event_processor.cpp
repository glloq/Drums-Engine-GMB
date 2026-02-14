#include "event_processor.h"
#include <math.h>

EventProcessor::EventProcessor(Scheduler* scheduler, EngineState* state)
  : _scheduler(scheduler), _state(state) {}

void EventProcessor::processMidiEvent(const MidiEvent& ev) {
  if (ev.type == MIDI_EVT_NOTE_ON && ev.data2 > 0) {
    // Note On: lookup pipeline
    uint8_t pipelineIdx = _lookup.note_to_pipeline[ev.data1];
    if (pipelineIdx == 0xFF || pipelineIdx >= _lookup.pipeline_count) return;

    _executePipeline(pipelineIdx, ev.data2, ev.timestamp);
  }
  else if (ev.type == MIDI_EVT_NOTE_OFF || (ev.type == MIDI_EVT_NOTE_ON && ev.data2 == 0)) {
    // Note Off: envoyer commande OFF a l'actionneur lie
    uint8_t pipelineIdx = _lookup.note_to_pipeline[ev.data1];
    if (pipelineIdx == 0xFF || pipelineIdx >= _lookup.pipeline_count) return;

    const CompiledPipeline& pipeline = _lookup.pipelines[pipelineIdx];
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
    // CC: stocker dans une variable globale indexee par CC number
    if (ev.data1 < MAX_GLOBAL_VARS) {
      _state->setVariable(ev.data1, ev.data2);
    }
  }
}

void EventProcessor::_executePipeline(uint8_t pipelineIdx, uint8_t value, uint32_t timestamp) {
  if (pipelineIdx >= _lookup.pipeline_count) return;

  const CompiledPipeline& pipeline = _lookup.pipelines[pipelineIdx];
  uint8_t currentValue = value;
  uint32_t delayAccum = 0; // Accumulation des delais

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
        // Les blocs TIME ajoutent du delai ou generent des impulsions
        if (block.subtype == TIME_DELAY) {
          delayAccum += (uint32_t)block.param2 * 1000; // ms -> us
        } else if (block.subtype == TIME_PULSE) {
          // Generer impulsion via scheduler
          uint32_t duration = (uint32_t)block.param2 * 1000; // ms -> us
          if (block.param1 > 0) {
            // Duree depend de la velocite
            duration = map(currentValue, 0, 127,
                          (uint32_t)block.param1 * 1000,
                          (uint32_t)block.param2 * 1000);
          }
          _scheduler->schedulePulseAt(pipeline.output_actuator_id,
                                       currentValue, duration,
                                       timestamp + delayAccum);
          return; // L'impulsion genere sa propre commande OFF
        }
        break;
      }

      case BlockType::OUTPUT: {
        _processOutputBlock(block, currentValue, timestamp + delayAccum);
        return; // Sortie atteinte
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

int16_t EventProcessor::_processConditionBlock(const CompiledBlock& block, uint8_t value,
                                                 uint8_t pipelineIdx) {
  switch (block.subtype) {
    case COND_THRESHOLD: {
      // param1: source variable index, param2: seuil
      uint16_t sourceVal;
      if (block.param1 == 0xFF) {
        sourceVal = value; // Utiliser la velocite
      } else {
        sourceVal = _state->getVariable(block.param1);
      }
      // bit 7 de param1 = direction (0: >, 1: <)
      bool isLessThan = (block.param1 & 0x80) != 0;
      if (isLessThan) {
        return (sourceVal < block.param2) ? value : -1;
      } else {
        return (sourceVal > block.param2) ? value : -1;
      }
    }

    case COND_ROUND_ROBIN: {
      // param2: nombre de sorties
      uint8_t numOutputs = block.param2 & 0xFF;
      uint8_t current = _state->advanceRoundRobin(pipelineIdx, numOutputs);
      // param1: index de sortie que ce pipeline gere
      if (current != block.param1) return -1; // Pas notre tour
      return value;
    }

    case COND_VELOCITY_SPLIT: {
      // param1: min, param2: max
      if (value >= block.param1 && value <= (block.param2 & 0xFF)) {
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
      // param2: gain * 100 (ex: 80 = 0.80, 150 = 1.50)
      uint16_t result = ((uint16_t)value * block.param2) / 100;
      return (result > 127) ? 127 : (uint8_t)result;
    }

    case TRANS_CLAMP: {
      // param1: min, param2: max
      if (value < block.param1) return block.param1;
      if (value > (block.param2 & 0xFF)) return (uint8_t)block.param2;
      return value;
    }

    default:
      return value;
  }
}

void EventProcessor::_processOutputBlock(const CompiledBlock& block, uint8_t value,
                                          uint32_t timestamp) {
  ActuatorCommand cmd;
  cmd.actuator_id = block.param1; // Actionneur cible dans param1
  cmd.value = value;
  cmd.execute_at = timestamp;

  switch (block.subtype) {
    case OUT_PULSE:
      cmd.command_type = (uint8_t)CommandType::PULSE;
      cmd.duration = block.param2; // Duree en centaines de us
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
  // Reinitialiser la lookup table
  memset(_lookup.note_to_pipeline, 0xFF, sizeof(_lookup.note_to_pipeline));
  // Les pipelines sont charges depuis le JSON par le pipeline compiler
  DBGF("[EventProc] Lookup recompiled (%d pipelines)\n", _lookup.pipeline_count);
}
