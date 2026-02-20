#ifndef EVENT_PROCESSOR_H
#define EVENT_PROCESSOR_H

#include "../core/types.h"
#include "../core/config.h"
#include "../core/global_state.h"
#include "../scheduler/scheduler.h"
#include "../led/led_event_queue.h"

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
// Supporte les 19 types de blocs :
//   CONDITION: Threshold, RoundRobin, VelocitySplit, Random, NoteRange
//   TRANSFORM: VelocityCurve, Gain, Clamp, Invert, SetVar
//   TIME: Pulse, Delay, Ramp, Gate
//   OUTPUT: Pulse, Position, PWM, Off
//
// MIDI events: NoteOn, NoteOff, CC, PitchBend (→CC#126), Aftertouch (→CC#125)
//
// Aucune allocation dynamique.
// ============================================================================

class EventProcessor {
public:
  struct CcDispatchStats {
    uint32_t received = 0;
    uint32_t throttled = 0;
    uint32_t routed_commands = 0;
    uint32_t routed_batches = 0;
    uint32_t unrouted = 0;
  };
  EventProcessor(Scheduler* scheduler, EngineState* state);

  // Traiter un evenement MIDI
  void processMidiEvent(const MidiEvent& ev);

  // Acces a la table de lookup
  PipelineLookup& getLookup() { return _lookup; }
  const PipelineLookup& getLookup() const { return _lookup; }
  const CcDispatchStats& getCcDispatchStats() const { return _ccStats; }

  // Get active note states (thread-safe snapshot)
  void getNoteActive(uint8_t* out) const;

  // Recharger/compiler les tables
  void recompileLookup();

  // LED event queue (Core 1 → Core 0)
  void setLedEventQueue(LedEventQueue* queue) { _ledQueue = queue; }

private:
  LedEventQueue* _ledQueue = nullptr;
  Scheduler* _scheduler;
  EngineState* _state;
  PipelineLookup _lookup;
  volatile bool _recompiling = false;  // C4 fix: flag to block Core 1 reads during recompilation
  uint32_t _lastCcDispatchUs[128];
  uint8_t _noteActive[128];  // Counter: 0=inactive, N=N stacked instances active

  CcDispatchStats _ccStats;

  // Executer un pipeline sur une valeur (midiNote passé pour COND_NOTE_RANGE)
  void _executePipeline(uint8_t pipelineIdx, uint8_t value, uint32_t timestamp, uint8_t midiNote = 0xFF);
  void _processCcEvent(const MidiEvent& ev);

  // Traitement des blocs individuels
  int16_t _processConditionBlock(const CompiledBlock& block, uint8_t value, uint8_t pipelineIdx, uint8_t midiNote);
  uint8_t _processTransformBlock(const CompiledBlock& block, uint8_t value);
  void _processOutputBlock(const CompiledBlock& block, uint8_t value,
                            uint32_t timestamp, uint32_t delayAccum);

  // TIME block handlers
  uint32_t _processTimePulse(const CompiledBlock& block, uint8_t actuatorId,
                              uint8_t value, uint32_t timestamp, uint32_t delayAccum);
  void _processTimeRamp(const CompiledBlock& block, uint8_t actuatorId,
                         uint8_t value, uint32_t timestamp, uint32_t delayAccum);

  // Courbes de velocite
  uint8_t _applyVelocityCurve(uint8_t value, uint8_t curveType);
};

#endif // EVENT_PROCESSOR_H
