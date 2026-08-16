#ifndef COMMAND_SINK_H
#define COMMAND_SINK_H

#include "../core/types.h"

// ============================================================================
// CommandSink - ce que l'EventProcessor attend d'un ordonnanceur
// ============================================================================
// L'EventProcessor tenait un `Scheduler*` concret, qui tient un
// ActuatorManager, qui tient des Actuator, qui tiennent des drivers I2C. Pour
// tester la couche qui decide CE QUI FRAPPE, il fallait donc toute la pile
// materielle — autrement dit on ne la testait pas.
//
// Cette interface est la surface REELLE utilisee par l'EventProcessor : trois
// methodes. Le Scheduler l'implemente, et les tests natifs branchent un
// enregistreur a la place (voir test/test_event_processor).
//
// Cout temps reel : un appel virtuel par commande. Le chemin en comporte deja
// (Actuator::execute est virtuel) et il est domine par le spinlock et
// l'insertion triee de la file — la difference n'est pas mesurable.
// ============================================================================

class CommandSink {
public:
  virtual ~CommandSink() {}

  // Mettre en file une commande isolee. false = refusee (file pleine).
  virtual bool scheduleCommand(const ActuatorCommand& cmd) = 0;

  // Impulsion differee : ON puis OFF, insere de facon transactionnelle.
  virtual bool schedulePulseAt(uint8_t actuatorId, uint16_t value,
                               uint32_t durationUs, uint32_t executeAt) = 0;

  // Sequence d'actions d'un evenement, TOUTE mise en file ou aucune.
  // Pas d'argument par defaut ici : sur une methode virtuelle il serait resolu
  // statiquement selon le type declare, ce qui ferait diverger silencieusement
  // l'appel via l'interface de l'appel direct. Les appelants passent 0.
  virtual bool scheduleActionSteps(const ActionStep* steps, uint8_t stepCount,
                                   uint8_t velocity, uint32_t timestamp,
                                   const uint16_t* globalVars,
                                   uint8_t activeGroup) = 0;
};

#endif // COMMAND_SINK_H
