#ifndef ENGINE_POWER_BUDGET_H
#define ENGINE_POWER_BUDGET_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Budget electrique - arbitrage des activations simultanees
// ============================================================================
// La protection de surcharge etait un simple compteur : au plus
// MAX_CONCURRENT_ACTIVE actionneurs actifs a la fois. Cela revient a supposer
// qu'ils consomment tous pareil, alors que l'ecart atteint un facteur 100 entre
// un servo de hi-hat (~20 mA) et un solenoide de grosse caisse (~2 A). Sur une
// petite alimentation la limite etait trop permissive ; sur une grosse
// installation elle devenait une limite MUSICALE arbitraire (8 frappes
// simultanees, alors que le xylophone seul peut en demander plus).
//
// L'arbitrage porte donc sur le courant reellement declare par actionneur, avec
// deux plafonds et une priorite. La decision elle-meme vit ici, sans etat ni
// materiel, pour etre testable (test/test_power_budget).
// ============================================================================

// Priorite d'un actionneur. Persistee dans actuators.json — ne jamais inserer
// de valeur, seulement APPENDRE.
//
// Les noms sont prefixes PRIO_ a dessein : Arduino.h definit LOW et HIGH comme
// des MACROS, qui seraient developpees jusque dans un enum class et casseraient
// la compilation du firmware.
enum class ActuatorPriority : uint8_t {
  PRIO_LOW = 0,          // Ornement: sacrifie en premier quand l'alim sature
  PRIO_NORMAL,           // Defaut
  PRIO_HIGH,             // Structure rythmique (kick, snare)
  PRIORITY_COUNT
};

inline const char* actuatorPriorityName(uint8_t p) {
  switch ((ActuatorPriority)p) {
    case ActuatorPriority::PRIO_LOW:    return "Low";
    case ActuatorPriority::PRIO_NORMAL: return "Normal";
    case ActuatorPriority::PRIO_HIGH:   return "High";
    default:                            return "Unknown";
  }
}

// Budget global. Chaque plafond est desactivable independamment (0 = pas de
// limite). Les defauts reproduisent le comportement historique : seul le nombre
// est limite tant que l'utilisateur n'a pas declare son alimentation.
struct PowerBudgetConfig {
  uint16_t maxPeakMa;        // Plafond dur: aucune activation ne le franchit
  uint16_t maxContinuousMa;  // Plafond souple: au-dela, LOW est refuse
  uint8_t maxConcurrent;     // Plafond en nombre d'actionneurs actifs

  PowerBudgetConfig()
    : maxPeakMa(POWER_BUDGET_PEAK_MA_DEF),
      maxContinuousMa(POWER_BUDGET_CONT_MA_DEF),
      maxConcurrent(MAX_CONCURRENT_ACTIVE) {}
};

enum class PowerVerdict : uint8_t {
  ADMIT = 0,
  REFUSED_COUNT,        // plafond en nombre atteint
  REFUSED_PEAK,         // depasserait le courant de crete
  REFUSED_CONTINUOUS    // ornement au-dela du courant continu
};

// Decider si une NOUVELLE activation est admise.
//   activeCount / activeMa : etat courant (actionneurs deja actifs)
//   requestMa              : courant declare par l'actionneur demande ;
//                            0 = non declare
//   priority               : ActuatorPriority de l'actionneur demande
//
// Un actionneur dont le courant n'est pas declare (requestMa == 0) ne consomme
// pas de budget et n'est jamais refuse pour un motif de courant. C'est ce qui
// rend la migration transparente : une configuration existante, ou aucun
// courant n'est renseigne, se comporte exactement comme avant.
inline PowerVerdict powerBudgetAdmit(const PowerBudgetConfig& budget,
                                     uint8_t activeCount, uint32_t activeMa,
                                     uint16_t requestMa, uint8_t priority) {
  if (budget.maxConcurrent > 0 && activeCount >= budget.maxConcurrent) {
    return PowerVerdict::REFUSED_COUNT;
  }
  if (requestMa == 0) return PowerVerdict::ADMIT;

  uint32_t projected = activeMa + requestMa;

  if (budget.maxPeakMa > 0 && projected > budget.maxPeakMa) {
    return PowerVerdict::REFUSED_PEAK;
  }
  // Au-dela de ce que l'alimentation tient en continu, on sacrifie les
  // ornements pour garder la structure rythmique.
  if (budget.maxContinuousMa > 0 && projected > budget.maxContinuousMa &&
      priority == (uint8_t)ActuatorPriority::PRIO_LOW) {
    return PowerVerdict::REFUSED_CONTINUOUS;
  }
  return PowerVerdict::ADMIT;
}

#endif // ENGINE_POWER_BUDGET_H
