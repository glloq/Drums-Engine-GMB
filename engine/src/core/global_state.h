#ifndef ENGINE_GLOBAL_STATE_H
#define ENGINE_GLOBAL_STATE_H

#include "types.h"

// ============================================================================
// Etat global du moteur - accessible en lecture aux blocs CONDITION
// ============================================================================
// Stockage global minimal, pas d'allocation dynamique
// Variables indexees utilisables dans les pipelines
// ============================================================================

class EngineState {
public:
  EngineState() {}

  // Variables globales (accessibles par index dans les blocs CONDITION)
  uint16_t getVariable(uint8_t index) const {
    if (index >= MAX_GLOBAL_VARS) return 0;
    return _state.variables[index];
  }

  void setVariable(uint8_t index, uint16_t value) {
    if (index < MAX_GLOBAL_VARS) {
      _state.variables[index] = value;
    }
  }

  // Round-robin counters (pour alternance dans pipelines)
  uint8_t getRoundRobin(uint8_t pipelineId) const {
    if (pipelineId >= MAX_PIPELINES) return 0;
    return _state.round_robin_counters[pipelineId];
  }

  uint8_t advanceRoundRobin(uint8_t pipelineId, uint8_t numOutputs) {
    if (pipelineId >= MAX_PIPELINES || numOutputs == 0) return 0;
    uint8_t current = _state.round_robin_counters[pipelineId];
    _state.round_robin_counters[pipelineId] = (current + 1) % numOutputs;
    return current;
  }

  GlobalState& raw() { return _state; }
  const GlobalState& raw() const { return _state; }

private:
  GlobalState _state;
};

#endif // ENGINE_GLOBAL_STATE_H
