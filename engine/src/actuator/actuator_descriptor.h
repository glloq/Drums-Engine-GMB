#ifndef ACTUATOR_DESCRIPTOR_H
#define ACTUATOR_DESCRIPTOR_H

#include "../core/types.h"
#include "../core/config.h"

// ============================================================================
// Description des types/comportements d'actionneur — SOURCE DE VERITE UNIQUE
// ============================================================================
// Trois endroits decrivaient jusqu'ici ce qu'un actionneur accepte :
//   1. le validateur      (actuator_validation.cpp, chaines de if)
//   2. GET /api/capabilities (routes_system.cpp, JSON ecrit a la main)
//   3. l'UI               (data/index.html, listes en dur)
// Ils avaient tous les trois derive. Au moment d'ecrire ce fichier :
//   - `capabilities` ignorait completement STEPPER ;
//   - il annoncait PCA9685 pour PWM_MOTOR, que le validateur refuse ;
//   - il n'annoncait PAS LEDC_PWM pour SOLENOID, alors que SOLENOID_HOLD
//     l'EXIGE : une UI pilotee par l'API ne pouvait pas creer un solenoide a
//     maintien ;
//   - SOLENOID n'annoncait aucun comportement, et SERVO oubliait SERVO_MUTE ;
//   - MOTOR_OPTICAL n'annoncait pas LEDC_PWM.
//
// La table ci-dessous est desormais la seule description. Le validateur et
// l'API la lisent, donc elles ne peuvent plus diverger : ajouter un
// comportement se fait a un seul endroit.
//
// La granularite est le COMPORTEMENT, pas le type : les contraintes de bus sont
// reellement par comportement (SOLENOID_STRIKE accepte MCP23017, SOLENOID_HOLD
// exige un vrai PWM). Le masque d'un type est l'union de ceux de ses
// comportements, ce qui se deduit au lieu d'etre saisi deux fois.
// ============================================================================

constexpr uint16_t busBit(HardwareBus b) { return (uint16_t)(1u << (uint8_t)b); }

constexpr uint16_t BUS_MCP  = busBit(HardwareBus::MCP23017);
constexpr uint16_t BUS_PCA  = busBit(HardwareBus::PCA9685);
constexpr uint16_t BUS_GPIO = busBit(HardwareBus::GPIO_DIRECT);
constexpr uint16_t BUS_LEDC = busBit(HardwareBus::LEDC_PWM);

struct BehaviorDescriptor {
  ActuatorBehavior behavior;
  ActuatorType type;        // type auquel ce comportement appartient
  uint16_t busMask;         // bus acceptes (OU de BUS_*)
  const char* name;
  // Message renvoye quand le bus choisi n'est pas dans busMask. Explique la
  // raison materielle plutot que de repeter la liste.
  const char* busErrMsg;
};

// Nombre de comportements decrits (doit couvrir tout l'enum).
uint8_t actuatorBehaviorDescriptorCount();
const BehaviorDescriptor* actuatorBehaviorDescriptors();

// Descripteur d'un comportement, ou nullptr s'il est hors enum.
const BehaviorDescriptor* actuatorBehaviorDescriptor(ActuatorBehavior b);

// Nom lisible d'un type, et bus recommande par defaut.
const char* actuatorTypeDisplayName(ActuatorType t);
HardwareBus actuatorTypeRecommendedBus(ActuatorType t);

// Masque des bus acceptes par un type = union de ceux de ses comportements.
uint16_t actuatorTypeBusMask(ActuatorType t);

// Le comportement appartient-il bien a ce type ?
bool actuatorBehaviorBelongsTo(ActuatorBehavior b, ActuatorType t);

// Ce type demande-t-il un service continu depuis la boucle temps reel ?
// Ces actionneurs occupent un emplacement rare (ActuatorManager::MAX_SERVICABLE),
// donc la factory doit pouvoir les compter AVANT de creer l'objet — sinon un
// actionneur serait accepte puis se revelerait immobile.
bool actuatorTypeNeedsContinuousService(ActuatorType t);

#endif // ACTUATOR_DESCRIPTOR_H
