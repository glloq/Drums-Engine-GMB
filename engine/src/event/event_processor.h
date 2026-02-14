#ifndef EVENT_PROCESSOR_H
#define EVENT_PROCESSOR_H

#include "../core/types.h"
#include "../core/config.h"
#include "../core/global_state.h"
#include "../scheduler/scheduler.h"

// ============================================================================
// EventProcessor - Moteur de traitement des evenements MIDI
// ============================================================================
// Classe centrale du moteur.
// Process interne :
//   1. Lookup pipeline index via table O(1)
//   2. Copier valeur (velocite / CC)
//   3. Appliquer blocs sequentiellement
//   4. Generer ActuatorCommand
//   5. Push dans scheduler queue
//
// Aucune allocation dynamique.
// ============================================================================

class EventProcessor {
public:
  EventProcessor(Scheduler* scheduler, EngineState* state);

  // Traiter un evenement MIDI
  void processMidiEvent(const MidiEvent& ev);

  // Acces a la table de lookup
  PipelineLookup& getLookup() { return _lookup; }
  const PipelineLookup& getLookup() const { return _lookup; }

  // Recharger/compiler les tables
  void recompileLookup();

private:
  Scheduler* _scheduler;
  EngineState* _state;
  PipelineLookup _lookup;

  // Executer un pipeline sur une valeur
  void _executePipeline(uint8_t pipelineIdx, uint8_t value, uint32_t timestamp);

  // Traitement des blocs individuels
  // Retourne la valeur transformee, ou -1 si le pipeline doit s'arreter
  int16_t _processConditionBlock(const CompiledBlock& block, uint8_t value, uint8_t pipelineIdx);
  uint8_t _processTransformBlock(const CompiledBlock& block, uint8_t value);
  void _processTimeBlock(const CompiledBlock& block, uint8_t actuatorId,
                          uint8_t value, uint32_t timestamp);
  void _processOutputBlock(const CompiledBlock& block, uint8_t value, uint32_t timestamp);

  // Courbes de velocite
  uint8_t _applyVelocityCurve(uint8_t value, uint8_t curveType);
};

#endif // EVENT_PROCESSOR_H
