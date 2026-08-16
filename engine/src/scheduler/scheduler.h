#ifndef ENGINE_SCHEDULER_H
#define ENGINE_SCHEDULER_H

#include "../core/types.h"
#include "../core/config.h"
#include "../actuator/actuator_manager.h"
#include "command_queue.h"

// ============================================================================
// Scheduler temps reel - Dispatch des commandes horodatees
// ============================================================================
// Architecture finale :
//   Timer ISR -> marque commandes pretes
//   -> ActuatorExecutionTask (priorite haute)
//   -> Drivers hardware
//
// Horodatage en microsecondes.
// Queue statique. Aucune allocation dynamique.
// Impulsions solenoide = 2 commandes (ON a t0, OFF a t0+duree).
// Ramping servo = N micro-steps pre-generes.
// ============================================================================

class Scheduler {
public:
  Scheduler(ActuatorManager* actuatorMgr);

  // Initialiser le scheduler
  void begin();

  // Ajouter une commande dans la queue
  bool scheduleCommand(const ActuatorCommand& cmd);

  // Creer une impulsion: ON maintenant, OFF apres duree
  bool schedulePulse(uint8_t actuatorId, uint16_t value, uint32_t durationUs);

  // Creer une impulsion differee
  bool schedulePulseAt(uint8_t actuatorId, uint16_t value,
                       uint32_t durationUs, uint32_t executeAt);


  // Planifier une sequence d'actions (multi-output) de facon TRANSACTIONNELLE :
  // toutes les commandes de l'evenement sont mises en file, ou aucune.
  // activeGroup: 0 = fire all, N = fire only steps with alternate_group==0 or ==N
  // Retourne false si le lot a ete refuse (file saturee) — l'appelant ne doit
  // alors marquer ni note active, ni avancement du round-robin.
  bool scheduleActionSteps(const ActionStep* steps, uint8_t stepCount,
                           uint8_t velocity, uint32_t timestamp,
                           const uint16_t* globalVars,
                           uint8_t activeGroup = 0);

  // Appeler dans loop() - traite les commandes pretes
  void update();

  // Annuler toutes les commandes
  void cancelAll();

  // Statistiques
  uint16_t queueCount() const { return _queue.count(); }
  float queueUsage() const { return _queue.usage(); }
  uint32_t getOverflowCount() const { return _queue.getOverflowCount(); }
  uint32_t getDispatchCount() const { return _dispatchCount; }
  // Evenements refuses en bloc parce que la file n'avait pas la place pour
  // toutes leurs commandes. A distinguer de getOverflowCount(), qui compte les
  // commandes individuelles.
  uint32_t getRejectedBatchCount() const { return _rejectedBatchCount; }

  // Mesure jitter
  uint32_t getLastJitterUs() const { return _lastJitterUs; }
  uint32_t getMaxJitterUs() const { return _maxJitterUs; }
  void resetJitterStats();

private:
  ActuatorManager* _actuatorMgr;
  CommandQueue _queue;

  // Statistiques
  uint32_t _dispatchCount;
  uint32_t _rejectedBatchCount;
  uint32_t _lastJitterUs;
  uint32_t _maxJitterUs;
};

#endif // ENGINE_SCHEDULER_H
