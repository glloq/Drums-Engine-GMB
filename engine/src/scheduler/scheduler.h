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

  // Appeler dans loop() - traite les commandes pretes
  void update();

  // Annuler toutes les commandes
  void cancelAll();

  // Statistiques
  uint16_t queueCount() const { return _queue.count(); }
  float queueUsage() const { return _queue.usage(); }
  uint32_t getOverflowCount() const { return _queue.getOverflowCount(); }
  uint32_t getDispatchCount() const { return _dispatchCount; }

  // Mesure jitter
  uint32_t getLastJitterUs() const { return _lastJitterUs; }
  uint32_t getMaxJitterUs() const { return _maxJitterUs; }
  void resetJitterStats();

private:
  ActuatorManager* _actuatorMgr;
  CommandQueue _queue;

  // Statistiques
  uint32_t _dispatchCount;
  uint32_t _lastJitterUs;
  uint32_t _maxJitterUs;
};

#endif // ENGINE_SCHEDULER_H
