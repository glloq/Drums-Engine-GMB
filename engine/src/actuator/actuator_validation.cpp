#include "actuator_validation.h"

bool validateActuatorConfig(const ActuatorConfig& cfg, const char*& errMsg) {
  // --- Enum sentinel checks (review #6) ---
  // An unknown type/bus would otherwise fall through every type-specific block
  // to `return true`.
  if ((uint8_t)cfg.type >= (uint8_t)ActuatorType::TYPE_COUNT) {
    errMsg = "Invalid actuator type";
    return false;
  }
  if ((uint8_t)cfg.bus >= (uint8_t)HardwareBus::BUS_COUNT) {
    errMsg = "Invalid hardware bus";
    return false;
  }

  // --- End-stop pins must be a valid GPIO (0..39) or 0xFF (disabled) ---
  if (cfg.endStopPin1 != 0xFF && cfg.endStopPin1 > 39) {
    errMsg = "endStopPin1 must be an ESP32 GPIO (0..39) or 255 (disabled)";
    return false;
  }
  if (cfg.endStopPin2 != 0xFF && cfg.endStopPin2 > 39) {
    errMsg = "endStopPin2 must be an ESP32 GPIO (0..39) or 255 (disabled)";
    return false;
  }

  // --- Generic direct-GPIO range check (P1 #15) ---
  // For buses driven straight off ESP32 pins, hwPin must be a real GPIO.
  if (cfg.bus == HardwareBus::GPIO_DIRECT || cfg.bus == HardwareBus::LEDC_PWM) {
    if (cfg.hwPin > 39) {
      errMsg = "hwPin must be a valid ESP32 GPIO (0..39) for direct GPIO/LEDC bus";
      return false;
    }
  }

  // --- I2C expander pin/channel range (review #6) ---
  // MCP23017: 16 GPIO (0..15). PCA9685: 16 PWM channels (0..15).
  if (cfg.bus == HardwareBus::MCP23017 && cfg.hwPin > 15) {
    errMsg = "MCP23017 pin must be in [0..15]";
    return false;
  }
  if (cfg.bus == HardwareBus::PCA9685 && cfg.hwPin > 15) {
    errMsg = "PCA9685 channel must be in [0..15]";
    return false;
  }

  // SOLENOID_HOLD: paramMin=pwmActivation, paramMax=pwmHold (paramMin CAN be > paramMax)
  // Other behaviors: paramMin must be <= paramMax
  if (cfg.behavior != ActuatorBehavior::SOLENOID_HOLD) {
    if (cfg.paramMin > cfg.paramMax) {
      errMsg = "paramMin must be <= paramMax";
      return false;
    }
    if (cfg.paramDefault < cfg.paramMin || cfg.paramDefault > cfg.paramMax) {
      errMsg = "paramDefault must be inside [paramMin, paramMax]";
      return false;
    }
  }

  if (cfg.type == ActuatorType::SERVO) {
    if (cfg.bus != HardwareBus::PCA9685) {
      errMsg = "Servo currently requires PCA9685 bus";
      return false;
    }
    if (cfg.hwPin > 15) {
      errMsg = "Servo channel must be in [0..15]";
      return false;
    }
    // M7 fix: Validate behavior matches servo type
    if (cfg.behavior != ActuatorBehavior::SERVO_POSITION &&
        cfg.behavior != ActuatorBehavior::SERVO_STRIKE &&
        cfg.behavior != ActuatorBehavior::COMB_BRUSH &&
        cfg.behavior != ActuatorBehavior::PITCH_BEND &&
        cfg.behavior != ActuatorBehavior::HIHAT_CONTROLLER &&
        cfg.behavior != ActuatorBehavior::SERVO_MUTE) {
      errMsg = "Servo behavior must be SERVO_POSITION, SERVO_STRIKE, COMB_BRUSH, PITCH_BEND, HIHAT_CONTROLLER or SERVO_MUTE";
      return false;
    }
  }

  if (cfg.type == ActuatorType::SOLENOID) {
    if (cfg.bus != HardwareBus::MCP23017 && cfg.bus != HardwareBus::GPIO_DIRECT) {
      errMsg = "Solenoid supports MCP23017 or GPIO_DIRECT";
      return false;
    }
    // M7 fix: Validate behavior matches solenoid type
    if (cfg.behavior != ActuatorBehavior::SOLENOID_STRIKE &&
        cfg.behavior != ActuatorBehavior::SOLENOID_HOLD &&
        cfg.behavior != ActuatorBehavior::SOLENOID_MUTE) {
      errMsg = "Solenoid behavior must be SOLENOID_STRIKE, SOLENOID_HOLD or SOLENOID_MUTE";
      return false;
    }
  }

  if (cfg.type == ActuatorType::PWM_MOTOR) {
    if (cfg.bus != HardwareBus::LEDC_PWM && cfg.bus != HardwareBus::PCA9685) {
      errMsg = "PWM motor supports LEDC_PWM or PCA9685";
      return false;
    }
    // Valider le behavior pour PWM_MOTOR
    if (cfg.behavior != ActuatorBehavior::MOTOR_TIMED &&
        cfg.behavior != ActuatorBehavior::MOTOR_SPEED &&
        cfg.behavior != ActuatorBehavior::MOTOR_SWEEP &&
        cfg.behavior != ActuatorBehavior::MOTOR_ALTERNATE) {
      errMsg = "PWM motor behavior must be MOTOR_TIMED, MOTOR_SPEED, MOTOR_SWEEP or MOTOR_ALTERNATE";
      return false;
    }
    // MOTOR_SWEEP requiert au moins un end stop
    // 2 end stops + hwAddress → aller-retour complet (direction pin requis)
    if (cfg.behavior == ActuatorBehavior::MOTOR_SWEEP) {
      if (cfg.endStopPin1 == 0xFF && cfg.endStopPin2 == 0xFF) {
        errMsg = "MOTOR_SWEEP requires at least one endStopPin";
        return false;
      }
      bool hasTwoEndStops = (cfg.endStopPin1 != 0xFF && cfg.endStopPin2 != 0xFF);
      bool hasDirPin = (cfg.hwAddress > 0 && cfg.hwAddress < 40);
      if (hasTwoEndStops && !hasDirPin) {
        errMsg = "MOTOR_SWEEP with 2 end stops requires hwAddress as direction GPIO pin (1-39)";
        return false;
      }
    }
    // MOTOR_ALTERNATE requiert end stop + direction pin (hwAddress)
    if (cfg.behavior == ActuatorBehavior::MOTOR_ALTERNATE) {
      if (cfg.endStopPin1 == 0xFF && cfg.endStopPin2 == 0xFF) {
        errMsg = "MOTOR_ALTERNATE requires at least one endStopPin";
        return false;
      }
      if (cfg.bus != HardwareBus::LEDC_PWM) {
        errMsg = "MOTOR_ALTERNATE requires LEDC_PWM bus (direction pin via hwAddress)";
        return false;
      }
      if (cfg.hwAddress == 0 || cfg.hwAddress >= 40) {
        errMsg = "MOTOR_ALTERNATE requires hwAddress as direction GPIO pin (1-39)";
        return false;
      }
    }
  }

  if (cfg.type == ActuatorType::MOTOR_OPTICAL) {
    if (cfg.bus != HardwareBus::GPIO_DIRECT) {
      errMsg = "Motor optical currently requires GPIO_DIRECT (sensor read not available on PWM-only drivers)";
      return false;
    }
    if (cfg.behavior != ActuatorBehavior::MOTOR_OPTICAL_TRACK) {
      errMsg = "Motor optical requires behavior MOTOR_OPTICAL_TRACK";
      return false;
    }
    if (cfg.hwAddress > 39) {
      errMsg = "Motor optical sensor pin (hwAddress) must be in [0..39]";
      return false;
    }
    if (cfg.hwPin > 39) {
      errMsg = "Motor optical drive pin (hwPin) must be in [0..39]";
      return false;
    }
    if (cfg.hwPin == cfg.hwAddress) {
      errMsg = "Motor optical drive pin and sensor pin must be different";
      return false;
    }
  }

  errMsg = nullptr;
  return true;
}
