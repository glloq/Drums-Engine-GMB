#include "actuator_validation.h"

// ----------------------------------------------------------------------------
// ESP32 (classic / WROOM-32) GPIO capability table (review #13)
// ----------------------------------------------------------------------------
bool esp32GpioExists(uint8_t pin) {
  if (pin > 39) return false;
  // Numbers not bonded out on the classic ESP32 (WROOM-32).
  if (pin == 20 || pin == 24 || (pin >= 28 && pin <= 31)) return false;
  return true;
}

bool esp32GpioIsFlashReserved(uint8_t pin) {
  return pin >= 6 && pin <= 11;  // SPI flash (SD2/SD3/CMD/CLK/SD0/SD1)
}

bool esp32GpioIsInputOnly(uint8_t pin) {
  return pin >= 34 && pin <= 39;  // no output driver on GPIO 34..39
}

bool esp32GpioCanOutput(uint8_t pin) {
  return esp32GpioExists(pin) && !esp32GpioIsFlashReserved(pin) && !esp32GpioIsInputOnly(pin);
}

bool esp32GpioCanInput(uint8_t pin) {
  return esp32GpioExists(pin) && !esp32GpioIsFlashReserved(pin);
}

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

  // --- End-stop pins must be input-capable GPIOs or 0xFF (disabled) ---
  // Input-only pins (34..39) are fine for a limit switch; flash pins are not.
  if (cfg.endStopPin1 != 0xFF && !esp32GpioCanInput(cfg.endStopPin1)) {
    errMsg = "endStopPin1 is not an input-capable ESP32 GPIO (or 255=disabled)";
    return false;
  }
  if (cfg.endStopPin2 != 0xFF && !esp32GpioCanInput(cfg.endStopPin2)) {
    errMsg = "endStopPin2 is not an input-capable ESP32 GPIO (or 255=disabled)";
    return false;
  }

  // --- Direct-GPIO output pins must actually be output-capable (review #13) ---
  // Rejects input-only (34..39), flash (6..11) and non-existent pins that would
  // silently produce no output.
  if (cfg.bus == HardwareBus::GPIO_DIRECT || cfg.bus == HardwareBus::LEDC_PWM) {
    if (!esp32GpioCanOutput(cfg.hwPin)) {
      errMsg = "hwPin must be an output-capable ESP32 GPIO (not input-only/flash) for direct GPIO/LEDC bus";
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
      if (hasDirPin && !esp32GpioCanOutput(cfg.hwAddress)) {   // review #13
        errMsg = "MOTOR_SWEEP direction pin (hwAddress) must be an output-capable GPIO";
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
      if (!esp32GpioCanOutput(cfg.hwAddress)) {   // review #13
        errMsg = "MOTOR_ALTERNATE direction pin (hwAddress) must be an output-capable GPIO";
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
    // Sensor pin (hwAddress) is read as an input; drive pin (hwPin) is an output.
    if (!esp32GpioCanInput(cfg.hwAddress)) {    // review #13
      errMsg = "Motor optical sensor pin (hwAddress) must be an input-capable ESP32 GPIO";
      return false;
    }
    if (!esp32GpioCanOutput(cfg.hwPin)) {       // review #13
      errMsg = "Motor optical drive pin (hwPin) must be an output-capable ESP32 GPIO";
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
