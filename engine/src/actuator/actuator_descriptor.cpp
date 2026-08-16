#include "actuator_descriptor.h"

// ----------------------------------------------------------------------------
// La table. Un comportement par ligne, avec les bus qu'il accepte reellement.
// ----------------------------------------------------------------------------
// Les raisons materielles derriere chaque restriction :
//   - SERVO : PCA9685 uniquement, la carte est cadencee a PCA_FREQUENCY (50 Hz)
//     pour produire les impulsions servo.
//   - SOLENOID_HOLD : LEDC_PWM obligatoire. MCP23017 et GPIO_DIRECT sont
//     tout-ou-rien, donc le niveau de maintien sortirait a pleine puissance —
//     risque thermique, pas simplement un son different.
//   - PWM_MOTOR : LEDC uniquement. PCA9685 tourne a 50 Hz, inutilisable pour
//     piloter un MOSFET/pont en H (ondulation de couple, dissipation).
//   - MOTOR_OPTICAL : le capteur est lu directement en GPIO, donc la pin de
//     commande peut utiliser LEDC (vraie vitesse) ou GPIO (tout-ou-rien).
//   - STEPPER : GPIO direct, les pas doivent etre comptes et cadences.
static const BehaviorDescriptor kBehaviors[] = {
  { ActuatorBehavior::SOLENOID_STRIKE, ActuatorType::SOLENOID,
    BUS_MCP | BUS_GPIO | BUS_LEDC, "Solenoid Strike",
    "Solenoid supports MCP23017, GPIO_DIRECT or LEDC_PWM" },
  { ActuatorBehavior::SOLENOID_HOLD, ActuatorType::SOLENOID,
    BUS_LEDC, "Solenoid Hold",
    "SOLENOID_HOLD requires the LEDC_PWM bus: MCP23017/GPIO_DIRECT have no "
    "hardware PWM, so the hold level would stay at full current (overheating risk)" },
  { ActuatorBehavior::SOLENOID_MUTE, ActuatorType::SOLENOID,
    BUS_MCP | BUS_GPIO | BUS_LEDC, "Solenoid Mute",
    "Solenoid supports MCP23017, GPIO_DIRECT or LEDC_PWM" },

  { ActuatorBehavior::SERVO_POSITION, ActuatorType::SERVO,
    BUS_PCA, "Servo Position", "Servo currently requires PCA9685 bus" },
  { ActuatorBehavior::SERVO_STRIKE, ActuatorType::SERVO,
    BUS_PCA, "Servo Strike", "Servo currently requires PCA9685 bus" },
  { ActuatorBehavior::COMB_BRUSH, ActuatorType::SERVO,
    BUS_PCA, "Comb/Brush", "Servo currently requires PCA9685 bus" },
  { ActuatorBehavior::PITCH_BEND, ActuatorType::SERVO,
    BUS_PCA, "Pitch Bend", "Servo currently requires PCA9685 bus" },
  { ActuatorBehavior::HIHAT_CONTROLLER, ActuatorType::SERVO,
    BUS_PCA, "Hi-Hat Controller", "Servo currently requires PCA9685 bus" },
  { ActuatorBehavior::SERVO_MUTE, ActuatorType::SERVO,
    BUS_PCA, "Servo Mute", "Servo currently requires PCA9685 bus" },

  { ActuatorBehavior::MOTOR_TIMED, ActuatorType::PWM_MOTOR,
    BUS_LEDC, "Motor Timed",
    "PWM motor requires LEDC_PWM (PCA9685 runs at 50 Hz for servos, "
    "which is unsuitable for motor drive)" },
  { ActuatorBehavior::MOTOR_SPEED, ActuatorType::PWM_MOTOR,
    BUS_LEDC, "Motor Speed",
    "PWM motor requires LEDC_PWM (PCA9685 runs at 50 Hz for servos, "
    "which is unsuitable for motor drive)" },
  { ActuatorBehavior::MOTOR_SWEEP, ActuatorType::PWM_MOTOR,
    BUS_LEDC, "Motor Sweep",
    "PWM motor requires LEDC_PWM (PCA9685 runs at 50 Hz for servos, "
    "which is unsuitable for motor drive)" },
  { ActuatorBehavior::MOTOR_ALTERNATE, ActuatorType::PWM_MOTOR,
    BUS_LEDC, "Motor Alternate",
    "MOTOR_ALTERNATE requires LEDC_PWM bus (direction pin via hwAddress)" },

  { ActuatorBehavior::MOTOR_OPTICAL_TRACK, ActuatorType::MOTOR_OPTICAL,
    BUS_GPIO | BUS_LEDC, "Motor Optical",
    "Motor optical requires GPIO_DIRECT or LEDC_PWM (LEDC_PWM for real speed control)" },

  { ActuatorBehavior::STEPPER_POSITION, ActuatorType::STEPPER,
    BUS_GPIO, "Stepper Position",
    "Stepper requires the GPIO_DIRECT bus (STEP/DIR are timed logic outputs; "
    "I2C expanders and LEDC cannot emit a counted step train)" },
  { ActuatorBehavior::STEPPER_STRIKE, ActuatorType::STEPPER,
    BUS_GPIO, "Stepper Strike",
    "Stepper requires the GPIO_DIRECT bus (STEP/DIR are timed logic outputs; "
    "I2C expanders and LEDC cannot emit a counted step train)" },
  { ActuatorBehavior::STEPPER_ROTATE, ActuatorType::STEPPER,
    BUS_GPIO, "Stepper Rotate",
    "Stepper requires the GPIO_DIRECT bus (STEP/DIR are timed logic outputs; "
    "I2C expanders and LEDC cannot emit a counted step train)" },
};

static constexpr uint8_t kBehaviorCount =
    sizeof(kBehaviors) / sizeof(kBehaviors[0]);

// Un comportement ajoute a l'enum sans ligne dans la table serait invisible de
// l'API et refuse par le validateur sans explication : le compilateur le
// signale ici.
static_assert(kBehaviorCount == (uint8_t)ActuatorBehavior::BEHAVIOR_COUNT,
              "every ActuatorBehavior needs a row in kBehaviors");

uint8_t actuatorBehaviorDescriptorCount() { return kBehaviorCount; }
const BehaviorDescriptor* actuatorBehaviorDescriptors() { return kBehaviors; }

const BehaviorDescriptor* actuatorBehaviorDescriptor(ActuatorBehavior b) {
  for (uint8_t i = 0; i < kBehaviorCount; i++) {
    if (kBehaviors[i].behavior == b) return &kBehaviors[i];
  }
  return nullptr;
}

const char* actuatorTypeDisplayName(ActuatorType t) {
  switch (t) {
    case ActuatorType::SERVO:         return "Servo";
    case ActuatorType::SOLENOID:      return "Solenoid";
    case ActuatorType::PWM_MOTOR:     return "PWM Motor";
    case ActuatorType::MOTOR_OPTICAL: return "Motor Optical";
    case ActuatorType::STEPPER:       return "Stepper";
    default:                          return "Unknown";
  }
}

// Bus propose par defaut dans l'UI. Toujours un membre du masque du type.
HardwareBus actuatorTypeRecommendedBus(ActuatorType t) {
  switch (t) {
    case ActuatorType::SERVO:         return HardwareBus::PCA9685;
    case ActuatorType::SOLENOID:      return HardwareBus::MCP23017;
    case ActuatorType::PWM_MOTOR:     return HardwareBus::LEDC_PWM;
    case ActuatorType::MOTOR_OPTICAL: return HardwareBus::GPIO_DIRECT;
    case ActuatorType::STEPPER:       return HardwareBus::GPIO_DIRECT;
    default:                          return HardwareBus::GPIO_DIRECT;
  }
}

uint16_t actuatorTypeBusMask(ActuatorType t) {
  uint16_t mask = 0;
  for (uint8_t i = 0; i < kBehaviorCount; i++) {
    if (kBehaviors[i].type == t) mask |= kBehaviors[i].busMask;
  }
  return mask;
}

bool actuatorBehaviorBelongsTo(ActuatorBehavior b, ActuatorType t) {
  const BehaviorDescriptor* d = actuatorBehaviorDescriptor(b);
  return d && d->type == t;
}

// Doit rester en accord avec Actuator::needsService() des classes concretes.
// Seul le stepper emet un mouvement continu ; les autres types sont pilotes
// entierement par des commandes ponctuelles du scheduler.
bool actuatorTypeNeedsContinuousService(ActuatorType t) {
  return t == ActuatorType::STEPPER;
}
