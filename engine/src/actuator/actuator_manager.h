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
//   - Protection surcharge : arbitrage sur un budget electrique (voir ci-dessous)
//
// Budget electrique
// -----------------
// La protection etait un simple compteur : au plus MAX_CONCURRENT_ACTIVE
// actionneurs simultanes. Cela revient a supposer qu'ils consomment tous
// pareil, ce qui est faux d'un facteur 100 entre un servo de hi-hat (~20 mA) et
// un solenoide de grosse caisse (~2 A). Sur une petite installation la limite
// etait trop laxiste pour l'alimentation, sur une grosse elle devenait une
// limite MUSICALE arbitraire.
//
// L'arbitrage porte desormais sur le courant reellement declare par actionneur
// (ActuatorConfig::currentMa), avec deux plafonds et une priorite :
//   maxPeakMa       plafond dur, jamais franchi ;
//   maxContinuousMa plafond souple : au-dela, seules les priorites NORMAL/HIGH
//                   passent, les ornements (LOW) sont refuses ;
//   maxConcurrent   plafond en nombre, conserve comme garde-fou independant.
// Un plafond a 0 est desactive, et un actionneur dont currentMa vaut 0 (courant
// non declare) ne consomme pas de budget — une configuration existante se
// comporte donc exactement comme avant tant que rien n'est declare.
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

  // Faire avancer les actionneurs a mouvement continu (steppers) — appele a
  // chaque passe de la boucle temps reel. Ne fait rien s'il n'y en a aucun.
  void serviceAll(uint32_t nowUs);

  // Nombre d'actionneurs actuellement actifs (cached, O(1))
  uint8_t getActiveCount() const { return _activeCount; }

  // Courant instantane consomme par les actionneurs actifs, en mA (cached)
  uint32_t getActiveCurrentMa() const { return _activeCurrentMa; }

  // --- Budget electrique ---
  const PowerBudgetConfig& getPowerBudget() const { return _budget; }
  void setPowerBudget(const PowerBudgetConfig& budget);

  // Somme des courants declares si TOUS les actionneurs etaient actifs en meme
  // temps. Sert a l'UI pour dimensionner l'alimentation et signaler un budget
  // configure en dessous du pire cas.
  uint32_t getWorstCaseCurrentMa() const;

  // Nombre d'activations refusees par le budget depuis le demarrage, par motif.
  uint32_t getRefusedByCount() const { return _refusedByCount; }
  uint32_t getRefusedByPeak() const { return _refusedByPeak; }
  uint32_t getRefusedByContinuous() const { return _refusedByContinuous; }
  void resetRefusedStats();

  // Mettre a jour le cache (appele par dispatch/watchdog)
  void updateActiveCount();

private:
  Actuator* _actuators[MAX_ACTUATORS];
  uint8_t _idMap[MAX_ACTUATORS];     // Map index -> actuator ID
  uint8_t _count;
  uint8_t _activeCount = 0;
  uint32_t _activeCurrentMa = 0;
  Scheduler* _scheduler = nullptr;

  PowerBudgetConfig _budget;
  uint32_t _refusedByCount = 0;
  uint32_t _refusedByPeak = 0;
  uint32_t _refusedByContinuous = 0;

  // Sous-ensemble des actionneurs qui demandent un service continu. Garde a
  // part pour que la boucle temps reel n'ait pas a parcourir tout le registre
  // a 1 kHz quand aucun stepper n'est configure (le cas courant).
  Actuator* _servicable[MAX_ACTUATORS];
  uint8_t _servicableCount = 0;

  int8_t _findIndex(uint8_t id) const;
  void _updateActiveCountLocked();  // Must be called with _actMgrMux held
  // Decide si `act` peut etre active. Doit etre appele avec _actMgrMux tenu.
  bool _admitLocked(const Actuator* act);
};

#endif // ACTUATOR_MANAGER_H
