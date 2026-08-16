#include "instrument_validation.h"
#include <string.h>

bool validateInstrumentConfig(InstrumentConfig& cfg, const char*& errMsg,
                              ActuatorLookupFn lookup, void* ctx) {
  if (cfg.midiNote >= 128) {
    errMsg = "midiNote must be in [0..127]";
    return false;
  }
  if (cfg.midiChannel > 16) {
    errMsg = "midiChannel must be 0 (all) or [1..16]";
    return false;
  }
  if (cfg.noteOnCount > MAX_ACTIONS_PER_EVENT || cfg.noteOffCount > MAX_ACTIONS_PER_EVENT) {
    errMsg = "Too many action steps";
    return false;
  }
  if (cfg.ccBindingCount > MAX_CC_BINDINGS) {
    errMsg = "Too many CC bindings";
    return false;
  }

  auto actuatorConfig = [&](uint8_t actuatorId) -> const ActuatorConfig* {
    if (actuatorId == 0xFF) return nullptr;
    return lookup ? lookup(ctx, actuatorId) : nullptr;
  };

  auto commandCompatibleWithActuator = [&](uint8_t commandType, const ActuatorConfig& actCfg) -> bool {
    // Un stepper accepte les trois commandes, mais chacune n'a de sens que pour
    // un comportement : PULSE frappe, POSITION positionne, PWM fait tourner.
    switch ((CommandType)commandType) {
      case CommandType::PULSE:
        return actCfg.type == ActuatorType::SOLENOID || actCfg.type == ActuatorType::SERVO
            || (actCfg.type == ActuatorType::STEPPER &&
                actCfg.behavior == ActuatorBehavior::STEPPER_STRIKE);
      case CommandType::POSITION:
        return actCfg.type == ActuatorType::SERVO || actCfg.type == ActuatorType::MOTOR_OPTICAL
            || (actCfg.type == ActuatorType::STEPPER &&
                actCfg.behavior != ActuatorBehavior::STEPPER_ROTATE);
      case CommandType::PWM:
        return actCfg.type == ActuatorType::PWM_MOTOR || actCfg.type == ActuatorType::MOTOR_OPTICAL
            || actCfg.type == ActuatorType::SERVO
            || (actCfg.type == ActuatorType::STEPPER &&
                actCfg.behavior == ActuatorBehavior::STEPPER_ROTATE);
      case CommandType::OFF:
        return true;
      default:
        return false;
    }
  };

  auto normalizeAndValidateStep = [&](ActionStep& step, bool isNoteOn) -> bool {
    if (step.command_type > (uint8_t)CommandType::OFF) {
      errMsg = isNoteOn ? "Invalid command_type in noteOnActions"
                        : "Invalid command_type in noteOffActions";
      return false;
    }
    if (step.value_source > (uint8_t)ValueSource::INVERTED_VELOCITY) {
      errMsg = isNoteOn ? "Invalid value_source in noteOnActions"
                        : "Invalid value_source in noteOffActions";
      return false;
    }
    if (step.velocity_curve > CURVE_LOG) {
      errMsg = "velocity_curve must be in [CURVE_LINEAR..CURVE_LOG]";
      return false;
    }
    const ActuatorConfig* actCfg = actuatorConfig(step.actuator_id);
    if (!actCfg) {
      errMsg = "Action step references unknown actuatorId";
      return false;
    }
    if (!commandCompatibleWithActuator(step.command_type, *actCfg)) {
      errMsg = "Action commandType incompatible with actuator type";
      return false;
    }
    if (step.value_source == (uint8_t)ValueSource::CC_VAR && step.cc_index >= MAX_GLOBAL_VARS) {
      errMsg = "cc_index out of range for CC_VAR source";
      return false;
    }
    if (step.duration_max_ms < step.duration_min_ms) {
      uint16_t t = step.duration_min_ms;
      step.duration_min_ms = step.duration_max_ms;
      step.duration_max_ms = t;
    }
    return true;
  };

  for (uint8_t i = 0; i < cfg.noteOnCount; i++) {
    if (!normalizeAndValidateStep(cfg.noteOnActions[i], true)) return false;
  }
  for (uint8_t i = 0; i < cfg.noteOffCount; i++) {
    if (!normalizeAndValidateStep(cfg.noteOffActions[i], false)) return false;
  }

  for (uint8_t i = 0; i < cfg.ccBindingCount; i++) {
    CCBinding& b = cfg.ccBindings[i];
    if (b.cc_number >= 128) {
      errMsg = "ccNumber must be in [0..127]";
      return false;
    }
    const ActuatorConfig* actCfg = actuatorConfig(b.actuator_id);
    if (!actCfg) {
      errMsg = "ccBinding references unknown actuatorId";
      return false;
    }
    if (b.command_type != (uint8_t)CommandType::POSITION && b.command_type != (uint8_t)CommandType::PWM) {
      errMsg = "ccBindings.commandType must be POSITION or PWM";
      return false;
    }
    if (!commandCompatibleWithActuator(b.command_type, *actCfg)) {
      errMsg = "ccBinding commandType incompatible with actuator type";
      return false;
    }
    if (b.curve > CURVE_LOG) {
      errMsg = "ccBindings.curve must be in [CURVE_LINEAR..CURVE_LOG]";
      return false;
    }
    if (b.range_max < b.range_min) {
      uint8_t t = b.range_min;
      b.range_min = b.range_max;
      b.range_max = t;
    }
    for (uint8_t j = i + 1; j < cfg.ccBindingCount; j++) {
      const CCBinding& other = cfg.ccBindings[j];
      if (other.cc_number == b.cc_number &&
          other.actuator_id == b.actuator_id &&
          other.command_type == b.command_type) {
        errMsg = "Duplicate ccBinding (ccNumber + actuatorId + commandType)";
        return false;
      }
    }
  }

  errMsg = nullptr;
  return true;
}
