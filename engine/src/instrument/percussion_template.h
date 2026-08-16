#ifndef PERCUSSION_TEMPLATE_H
#define PERCUSSION_TEMPLATE_H

#include "../core/types.h"
#include "../core/config.h"

// ============================================================================
// Modeles de percussion — SOURCE DE VERITE UNIQUE
// ============================================================================
// Un modele etait decrit a TROIS endroits dans routes_system.cpp :
//   1. GET /api/templates       : ce que le modele DIT exiger (slotHints)
//   2. la chaine de requireSlot(): ce que la creation VERIFIE reellement
//   3. la chaine de construction : ce qu'elle FABRIQUE
// Elles avaient derive :
//   - le hi-hat annonce un servo HIHAT_CONTROLLER (le comportement qui porte
//     justement la logique CC#4 + splash) mais la creation exigeait
//     SERVO_POSITION : suivre l'exigence publiee faisait echouer la creation ;
//   - chaque modele annonce sa note par defaut, mais la construction partait de
//     `doc["midiNote"] | 36` — un hi-hat cree sans note explicite atterrissait
//     sur 36 (grosse caisse) au lieu de 42 ;
//   - la timbale liait son pitch au CC#127 alors que le moteur convertit le
//     Pitch Bend vers VIRTUAL_CC_PITCH_BEND (126) : la liaison ne recevait
//     jamais rien.
//
// La table ci-dessous est desormais la seule description. Elle ne depend que
// des types du moteur (pas d'ArduinoJson), donc chaque modele est verifiable
// par les tests natifs — c'est ce qui empeche la derive de revenir.
// ============================================================================

// Un emplacement d'actionneur exige par un modele.
// `behavior == BEHAVIOR_COUNT` signifie « n'importe quel comportement de ce
// type » (le shaker accepte tout moteur PWM, par exemple).
struct TemplateSlot {
  ActuatorType type;
  ActuatorBehavior behavior;
};

constexpr ActuatorBehavior ANY_BEHAVIOR = ActuatorBehavior::BEHAVIOR_COUNT;
constexpr uint8_t MAX_TEMPLATE_SLOTS = 3;

struct PercussionTemplate {
  const char* id;
  const char* name;
  const char* category;
  uint8_t defaultNote;
  uint8_t defaultChannel;
  uint8_t slotCount;
  TemplateSlot slots[MAX_TEMPLATE_SLOTS];

  // Remplit les actions/liaisons. `inst` arrive avec nom, categorie, note et
  // canal deja poses ; `actuatorIds` a MAX_TEMPLATE_SLOTS entrees (0xFF = absent).
  void (*build)(InstrumentConfig& inst, const uint8_t* actuatorIds);
};

uint8_t percussionTemplateCount();
const PercussionTemplate* percussionTemplates();

// Recherche par identifiant, nullptr si inconnu.
const PercussionTemplate* findPercussionTemplate(const char* id);

#endif // PERCUSSION_TEMPLATE_H
