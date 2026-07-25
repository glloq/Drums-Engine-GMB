#ifndef ACTUATOR_MANAGER_H
#define ACTUATOR_MANAGER_H

#include "actuator.h"
#include "../core/config.h"
#include "../core/types.h"

class Scheduler; // Forward declaration

// ============================================================================
// ActuatorManager - Gestionnaire central des actionneurs
// ============================================================================
// Registre des actionneurs. Dispatch les commandes vers le bon actuator.
// Ne connait pas le MIDI ni les pipelines.
//
// Securite :
//   - Watchdog periodique : force l'arret des actionneurs depasses
//   - Protection surcharge : limite les activations simultanees
// ============================================================================

class ActuatorManager {
public:
  ActuatorManager();

  // Enregistrer un actionneur (prend possession du pointeur)
  bool registerActuator(uint8_t id, Actuator* actuator);

  // Retirer un actionneur
  bool unregisterActuator(uint8_t id);

  // Dispatcher une commande vers l'actionneur cible
  void dispatch(const ActuatorCommand& cmd);

  // Arreter tous les actionneurs
  void stopAll();

  // Supprimer tous les enregistrements (pour rebuild)
  void clearAll();

  // Initialiser tous les actionneurs enregistres
  void initAll();

  // Acces aux actionneurs
  Actuator* getActuator(uint8_t id);
  const Actuator* getActuator(uint8_t id) const;
  uint8_t getCount() const { return _count; }

  // Test direct d'un actionneur (utilise le scheduler pour les solenoide)
  void testActuator(uint8_t id, uint8_t value);
  // Test servo: angle brut 0-180 (bypass paramMin/paramMax)
  void testServoAngle(uint8_t id, uint8_t angle);
  void setScheduler(class Scheduler* sched) { _scheduler = sched; }

  // --- Safety ---

  // Verifier les timeouts thermiques de tous les actionneurs (~10 Hz)
  uint8_t checkWatchdog();

  // Verifier les fins de course de tous les actionneurs (~1 kHz, review #5).
  // Separe du watchdog thermique pour une reaction rapide aux butees.
  uint8_t checkEndStops();

  // Nombre d'actionneurs actuellement actifs (cached, O(1))
  uint8_t getActiveCount() const { return _activeCount; }

  // Mettre a jour le cache (appele par dispatch/watchdog)
  void updateActiveCount();

private:
  Actuator* _actuators[MAX_ACTUATORS];
  uint8_t _idMap[MAX_ACTUATORS];     // Map index -> actuator ID
  uint8_t _count;
  uint8_t _activeCount = 0;
  Scheduler* _scheduler = nullptr;

  int8_t _findIndex(uint8_t id) const;
  void _updateActiveCountLocked();  // Must be called with _actMgrMux held
};

#endif // ACTUATOR_MANAGER_H
