#ifndef GMB_CAPABILITIES_H
#define GMB_CAPABILITIES_H

#include "../core/types.h"
#include "../core/config.h"
#include "../core/power_budget.h"

// ============================================================================
// CapabilitySnapshot — ce que la configuration ACTIVE sait jouer, en MIDI
// ============================================================================
// Drums-Engine empile quatre niveaux :
//
//     actionneur physique  ->  ActionStep / pipeline  ->  instrument logique
//                                                          ->  note / CC MIDI
//
// General-Midi-Boop ne s'interesse qu'au dernier. Ce module fait la traduction,
// une fois, a l'activation d'une configuration — et NULLE PART ailleurs : aucun
// calcul de capacite ne doit se retrouver dans une classe d'actionneur ou sur
// un chemin temps reel.
//
// La source de verite est la table de pipelines COMPILEE (PipelineLookup),
// c'est-a-dire la configuration validee et reellement active, pas la liste
// d'instruments telle qu'elle a ete saisie. C'est ce qui rend le resultat
// automatiquement correct sur les points ou le moteur a deja tranche :
//   - un instrument desactive n'a pas de pipeline, donc pas de note annoncee ;
//   - un instrument OMNI est deja deplie sur les 16 canaux, avec la precedence
//     "canal explicite > OMNI" appliquee ;
//   - deux actionneurs derriere la meme note ne donnent qu'UNE entree dans la
//     table (canal, note), donc la note ne peut pas apparaitre deux fois.
//
// Le module ne depend ni d'ArduinoJson, ni du materiel, ni du systeme de
// fichiers : il se teste en natif (test/test_gmb_capabilities).
// ============================================================================

// --- Role MIDI d'un actionneur -----------------------------------------------
// Un actionneur n'est pas une capacite musicale en soi ; c'est ce qu'il FAIT
// dans le pipeline qui en est une. Le role decide de la place que l'actionneur
// occupe dans le descripteur : voix sonore, etouffoir, pedale, accordage.
enum class GmbRole : uint8_t {
  STRIKE = 0,   // produit le son            -> descripteur "voices"
  DAMP,         // etouffoir / mute / choke  -> physical.choke_groups
  PEDAL,        // controleur hi-hat         -> physical.hihat
  POSITION,     // pre-positionnement silencieux (zone de frappe)
  TUNE          // tension de peau / pitch   -> physical.tuning
};

GmbRole gmbRoleForBehavior(ActuatorBehavior behavior);
const char* gmbRoleName(GmbRole role);

// Un ActionStep repond-il reellement a la velocite ?
// Ce n'est pas parce que value_source vaut VELOCITY que la machine l'entend :
// une impulsion dont les durees min et max sont egales frappe toujours pareil,
// et SOLENOID_HOLD ignore purement et simplement la valeur (solenoid_actuator
// applique paramMin comme PWM d'activation fixe).
bool gmbStepIsVelocitySensitive(const ActionStep& step, const ActuatorConfig& cfg);

// Intervalle minimal entre deux frappes du meme actionneur, en ms : le cooldown
// declare, jamais moins que la duree d'impulsion la plus longue (une bobine ne
// se redeclenche pas tant qu'elle est encore alimentee).
uint16_t gmbStepRearticulationMs(const ActionStep& step, const ActuatorConfig& cfg);

// --- Ensemble de notes MIDI (128 bits) ---------------------------------------
struct GmbNoteSet {
  uint8_t bits[16];

  void clear() { memset(bits, 0, sizeof(bits)); }
  void add(uint8_t note) { if (note < 128) bits[note >> 3] |= (uint8_t)(1u << (note & 7)); }
  bool has(uint8_t note) const {
    return note < 128 && (bits[note >> 3] & (uint8_t)(1u << (note & 7))) != 0;
  }
  uint8_t count() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t b = bits[i];
      while (b) { n = (uint8_t)(n + (b & 1)); b = (uint8_t)(b >> 1); }
    }
    return n;
  }
  bool empty() const {
    for (uint8_t i = 0; i < 16; i++) if (bits[i]) return false;
    return true;
  }
};

// --- Un actionneur vu depuis le MIDI -----------------------------------------
constexpr uint8_t GMB_MECH_VELOCITY = 0x01;

struct GmbMechanism {
  uint8_t actuatorId;
  uint8_t role;                // GmbRole
  uint8_t flags;               // GMB_MECH_VELOCITY
  uint16_t rearticulationMs;   // 0 = inconnu
  uint16_t currentMa;          // courant declare (0 = non declare)
  GmbNoteSet notes;            // notes de CET instrument que l'actionneur sert
};

// --- Modele temporel (SYSEX_IDENTITY.md §5.6) --------------------------------
// `known` dit quels champs sont derivables : un champ absent vaut INCONNU cote
// GMB, jamais zero, et l'utilisateur garde la main dessus.
constexpr uint8_t GMB_T_PREPARE        = 0x01;
constexpr uint8_t GMB_T_EXCITE         = 0x02;
constexpr uint8_t GMB_T_REARTICULATION = 0x04;
constexpr uint8_t GMB_T_RELEASE        = 0x08;

struct GmbTiming {
  uint16_t prepareMaxMs;       // fenetre de positionnement silencieux
  uint16_t exciteLatencyMs;    // commande -> frappe audible
  uint16_t rearticulationMs;   // intervalle minimal entre deux frappes
  uint16_t releaseMs;          // NoteOff -> relachement termine
  uint8_t jitterMs;            // granularite du scheduler
  uint8_t known;               // GMB_T_*
};

// --- Un instrument logique = un canal MIDI -----------------------------------
constexpr uint8_t GMB_INST_CONFIGURED = 0x01;
constexpr uint8_t GMB_INST_VELOCITY   = 0x02;
constexpr uint8_t GMB_INST_AFTERTOUCH = 0x04;
constexpr uint8_t GMB_INST_PITCHBEND  = 0x08;

struct GmbInstrumentCaps {
  uint8_t channel;             // 0-based, EXACTEMENT ce que declare le descripteur
  uint8_t flags;               // GMB_INST_*
  uint8_t polyphonyMax;        // 0 = inconnu (champ omis)
  uint8_t hihatPedalCc;        // 0xFF = aucune pedale hi-hat
  uint8_t mechFirst;           // index dans CapabilitySnapshot::mechanisms
  uint8_t mechCount;
  GmbNoteSet notes;            // notes jouables (mode "discrete")
  GmbNoteSet ccs;              // CC reellement routes (hors CC virtuels)
  GmbTiming timing;
};

struct CapabilitySnapshot {
  GmbInstrumentCaps instruments[GMB_MAX_LOGICAL_INSTRUMENTS];
  uint8_t instrumentCount;
  GmbMechanism mechanisms[GMB_MAX_MECHANISMS];
  uint8_t mechanismCount;
  // Plafond global du budget electrique : contrainte transverse a tous les
  // instruments, exposee comme `max_simultaneous_per_group` (§5.5).
  uint8_t globalMaxSimultaneous;   // 0 = pas de plafond declare
  // Mecanismes qui n'ont pas tenu dans le pool : signales, jamais tus.
  uint16_t droppedMechanisms;

  void clear() {
    memset(this, 0, sizeof(*this));
  }
};

// --- Entrees de la construction ----------------------------------------------
// Le resolveur d'actionneur est un pointeur de fonction, comme dans
// instrument_validation.h : l'API le branche sur le pool de la factory, le
// moteur sur l'ActuatorManager, et un test sur un tableau local.
typedef const ActuatorConfig* (*GmbActuatorLookupFn)(void* ctx, uint8_t actuatorId);

struct GmbBuildInputs {
  const PipelineLookup* lookup = nullptr;
  GmbActuatorLookupFn actuatorLookup = nullptr;
  void* actuatorCtx = nullptr;
  // Filtre de canaux de MidiEngine (bit 0 = canal 1). Un canal filtre n'est pas
  // routable : ses notes ne sont donc pas des capacites.
  uint16_t channelMask = 0xFFFF;
  PowerBudgetConfig power;
  // Canal (0-based) de repli quand seuls des instruments OMNI existent.
  uint8_t defaultChannel = GMB_DEFAULT_CHANNEL;
};

// Construire l'instantane. Deterministe : memes entrees, meme sortie, octet
// pour octet — c'est ce qui permet de detecter un changement de capacites en
// comparant deux descripteurs.
void gmbBuildCapabilities(const GmbBuildInputs& in, CapabilitySnapshot& out);

#endif // GMB_CAPABILITIES_H
