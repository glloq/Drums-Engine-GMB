#include "percussion_template.h"
#include <string.h>

// ----------------------------------------------------------------------------
// Constructeurs. Chacun ne remplit que les actions et liaisons : le nom, la
// categorie, la note et le canal viennent de la table et sont poses par
// l'appelant, ce qui garantit que la note par defaut ANNONCEE est bien celle
// APPLIQUEE.
// ----------------------------------------------------------------------------

static void buildStrike(InstrumentConfig& inst, const uint8_t* act,
                        uint16_t durMinMs, uint16_t durMaxMs) {
  inst.noteOnCount = 1;
  inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
  inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
  inst.noteOnActions[0].duration_min_ms = durMinMs;
  inst.noteOnActions[0].duration_max_ms = durMaxMs;
  inst.noteOnActions[0].actuator_id = act[0];
  inst.noteOffCount = 1;
  inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
  inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
  inst.noteOffActions[0].actuator_id = act[0];
}

static void buildKick(InstrumentConfig& inst, const uint8_t* act) {
  buildStrike(inst, act, 8, 25);
}

static void buildHihat(InstrumentConfig& inst, const uint8_t* act) {
  // Frappe + suivi continu de la pedale. Le servo est un HIHAT_CONTROLLER :
  // c'est lui qui porte la lecture CC#4 et la detection de splash.
  inst.noteOnCount = 2;
  inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
  inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
  inst.noteOnActions[0].duration_min_ms = 8;
  inst.noteOnActions[0].duration_max_ms = 25;
  inst.noteOnActions[0].actuator_id = act[0];
  inst.noteOnActions[1].command_type = (uint8_t)CommandType::POSITION;
  inst.noteOnActions[1].value_source = (uint8_t)ValueSource::CC_VAR;
  inst.noteOnActions[1].cc_index = 4;
  inst.noteOnActions[1].actuator_id = act[1];

  inst.ccBindingCount = 1;
  inst.ccBindings[0].cc_number = 4;
  inst.ccBindings[0].command_type = (uint8_t)CommandType::POSITION;
  inst.ccBindings[0].range_min = 30;
  inst.ccBindings[0].range_max = 150;
  inst.ccBindings[0].actuator_id = act[1];
}

static void buildCymbalMute(InstrumentConfig& inst, const uint8_t* act) {
  inst.noteOnCount = 1;
  inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
  inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
  inst.noteOnActions[0].duration_min_ms = 8;
  inst.noteOnActions[0].duration_max_ms = 20;
  inst.noteOnActions[0].actuator_id = act[0];

  inst.noteOffCount = 2;
  inst.noteOffActions[0].command_type = (uint8_t)CommandType::POSITION;
  inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
  inst.noteOffActions[0].value_fixed = 20;
  inst.noteOffActions[0].delay_ms = 50;
  inst.noteOffActions[0].actuator_id = act[1];
  inst.noteOffActions[1].command_type = (uint8_t)CommandType::OFF;
  inst.noteOffActions[1].value_source = (uint8_t)ValueSource::FIXED;
  inst.noteOffActions[1].actuator_id = act[0];
}

static void buildDoubleHit(InstrumentConfig& inst, const uint8_t* act) {
  inst.noteOnCount = 2;
  inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
  inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
  inst.noteOnActions[0].duration_min_ms = 8;
  inst.noteOnActions[0].duration_max_ms = 22;
  inst.noteOnActions[0].actuator_id = act[0];
  inst.noteOnActions[1] = inst.noteOnActions[0];
  inst.noteOnActions[1].actuator_id = act[1];
  inst.noteOffCount = 1;
  inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
  inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
  inst.noteOffActions[0].actuator_id = act[0];
}

static void buildShaker(InstrumentConfig& inst, const uint8_t* act) {
  inst.noteOnCount = 1;
  inst.noteOnActions[0].command_type = (uint8_t)CommandType::PWM;
  inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
  inst.noteOnActions[0].actuator_id = act[0];
  inst.noteOffCount = 1;
  inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
  inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
  inst.noteOffActions[0].actuator_id = act[0];
}

static void buildBrush(InstrumentConfig& inst, const uint8_t* act) {
  inst.noteOnCount = 1;
  inst.noteOnActions[0].command_type = (uint8_t)CommandType::PWM;
  inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
  inst.noteOnActions[0].actuator_id = act[0];
  inst.ccBindingCount = 1;
  inst.ccBindings[0].cc_number = 1;           // molette de modulation
  inst.ccBindings[0].command_type = (uint8_t)CommandType::PWM;
  inst.ccBindings[0].range_min = 20;
  inst.ccBindings[0].range_max = 127;
  inst.ccBindings[0].actuator_id = act[0];
  inst.noteOffCount = 1;
  inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
  inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
  inst.noteOffActions[0].actuator_id = act[0];
}

static void buildTimpani(InstrumentConfig& inst, const uint8_t* act) {
  inst.noteOnCount = 1;
  inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;
  inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
  inst.noteOnActions[0].duration_min_ms = 10;
  inst.noteOnActions[0].duration_max_ms = 30;
  inst.noteOnActions[0].actuator_id = act[0];

  inst.ccBindingCount = 1;
  // La tension de peau suit le Pitch Bend. Le moteur convertit le Pitch Bend
  // vers VIRTUAL_CC_PITCH_BEND ; la liaison etait sur le CC#127 et ne recevait
  // donc jamais rien (127 est aussi le vrai "Poly Mode On", d'ou le choix de
  // 126 comme CC virtuel).
  inst.ccBindings[0].cc_number = VIRTUAL_CC_PITCH_BEND;
  inst.ccBindings[0].command_type = (uint8_t)CommandType::POSITION;
  inst.ccBindings[0].range_min = 30;
  inst.ccBindings[0].range_max = 110;
  inst.ccBindings[0].actuator_id = (act[1] == 0xFF) ? act[0] : act[1];
}

static void buildMotorOptical(InstrumentConfig& inst, const uint8_t* act) {
  inst.noteOnCount = 1;
  inst.noteOnActions[0].command_type = (uint8_t)CommandType::POSITION;
  inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
  inst.noteOnActions[0].actuator_id = act[0];
  inst.noteOffCount = 1;
  inst.noteOffActions[0].command_type = (uint8_t)CommandType::OFF;
  inst.noteOffActions[0].value_source = (uint8_t)ValueSource::FIXED;
  inst.noteOffActions[0].actuator_id = act[0];
}

// ----------------------------------------------------------------------------
// La table
// ----------------------------------------------------------------------------
static const PercussionTemplate kTemplates[] = {
  { "kick", "Kick simple", "drums", 36, 10, 1,
    { { ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE } },
    buildKick },

  { "hihat", "Hi-Hat (CC4 + servo)", "cymbals", 42, 10, 2,
    { { ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE },
      { ActuatorType::SERVO,    ActuatorBehavior::HIHAT_CONTROLLER } },
    buildHihat },

  // NB: l'etouffoir attend un SERVO_POSITION et non un SERVO_MUTE. C'est ce que
  // l'ancienne declaration ET l'ancienne verification exigeaient toutes les
  // deux — elles ne divergeaient pas ici, donc le comportement est conserve tel
  // quel. Proposer une variante SERVO_MUTE releve du travail sur les presets,
  // pas de la correction de divergence.
  { "cymbal_mute", "Cymbal + mute", "cymbals", 49, 10, 2,
    { { ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE },
      { ActuatorType::SERVO,    ActuatorBehavior::SERVO_POSITION } },
    buildCymbalMute },

  { "double_hit", "Tom double frappe", "drums", 45, 10, 2,
    { { ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE },
      { ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE } },
    buildDoubleHit },

  { "shaker", "Shaker / Maracas", "latin", 82, 10, 1,
    { { ActuatorType::PWM_MOTOR, ANY_BEHAVIOR } },
    buildShaker },

  { "brush", "Brush (Comb)", "drums", 38, 10, 1,
    { { ActuatorType::PWM_MOTOR, ANY_BEHAVIOR } },
    buildBrush },

  { "timpani", "Timpani pitch", "drums", 47, 10, 2,
    { { ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE },
      { ActuatorType::SERVO,    ActuatorBehavior::PITCH_BEND } },
    buildTimpani },

  { "motor_optical", "Motor + capteur optique", "other", 60, 10, 1,
    { { ActuatorType::MOTOR_OPTICAL, ActuatorBehavior::MOTOR_OPTICAL_TRACK } },
    buildMotorOptical },
};

static constexpr uint8_t kTemplateCount =
    sizeof(kTemplates) / sizeof(kTemplates[0]);

uint8_t percussionTemplateCount() { return kTemplateCount; }
const PercussionTemplate* percussionTemplates() { return kTemplates; }

const PercussionTemplate* findPercussionTemplate(const char* id) {
  if (!id || id[0] == '\0') return nullptr;
  for (uint8_t i = 0; i < kTemplateCount; i++) {
    if (strcmp(kTemplates[i].id, id) == 0) return &kTemplates[i];
  }
  return nullptr;
}
