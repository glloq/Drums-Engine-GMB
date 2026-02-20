#include "solenoid_actuator.h"

SolenoidActuator::SolenoidActuator(HalDriver* driver)
  : _driver(driver), _holdTransitioned(false) {}

void SolenoidActuator::init() {
  if (!_driver || !_driver->isReady()) return;
  // Assurer que la pin est LOW au demarrage
  _driver->digitalWrite(_config.hwPin, false);
  _active = false;
  DBGF("[Actuator] Solenoid '%s' init (pin=%d)\n", _config.name, _config.hwPin);
}

void SolenoidActuator::execute(const ActuatorCommand& cmd) {
  if (!_driver || !_driver->isReady() || !_config.enabled) return;

  uint32_t now = micros();

  switch ((CommandType)cmd.command_type) {
    case CommandType::PULSE:
      // Verifier cooldown avant activation (HOLD n'utilise pas de cooldown)
      if (_config.behavior != ActuatorBehavior::SOLENOID_HOLD && !checkCooldown(now)) {
        DBGF("[Actuator] Solenoid '%s' PULSE rejected (cooldown, %lu us remaining)\n",
             _config.name, (unsigned long)((uint32_t)_config.cooldownUs * 100 - (now - _config.lastActivation)));
        return; // Encore en cooldown, ignorer la commande
      }

      // SOLENOID_MUTE: PULSE toggles state (energized = libre, de-energized = mute)
      if (_config.behavior == ActuatorBehavior::SOLENOID_MUTE) {
        if (_active) {
          stop();
        } else {
          _driver->digitalWrite(_config.hwPin, !_config.inverted);
          markActivation(now);
        }
        break;
      }

      // Activation: ecrire HIGH (ou PWM pour solenoid hold)
      if (_config.behavior == ActuatorBehavior::SOLENOID_HOLD) {
        // Start at full activation PWM (paramMin stores activation level 0-255)
        uint16_t activationPwm = map(_config.paramMin, 0, 255, 0, 4095);
        _driver->pwmWrite(_config.hwPin, activationPwm);
        _holdTransitioned = false;
      } else {
        _driver->digitalWrite(_config.hwPin, !_config.inverted);
        // C1 fix: Store strike duration for auto-stop in checkTimeout.
        // cmd.duration is in us/100; if 0, fallback to paramMax (ms)
        _strikeDurationUs = (cmd.duration > 0)
          ? (uint32_t)cmd.duration * 100
          : (uint32_t)_config.paramMax * 1000;
      }
      markActivation(now);
      break;

    case CommandType::POSITION:
      // SOLENOID_MUTE: explicit position control (< 64 = mute/contact, >= 64 = libre)
      if (_config.behavior == ActuatorBehavior::SOLENOID_MUTE) {
        if (cmd.value >= 64) {
          _driver->digitalWrite(_config.hwPin, !_config.inverted);
          markActivation(now);
        } else {
          stop();
        }
      }
      break;

    case CommandType::OFF:
      stop();
      break;

    default:
      break;
  }
}

void SolenoidActuator::stop() {
  if (!_driver || !_driver->isReady()) return;
  if (_config.behavior == ActuatorBehavior::SOLENOID_HOLD) {
    _driver->pwmWrite(_config.hwPin, 0);
    _holdTransitioned = false;
  } else {
    _driver->digitalWrite(_config.hwPin, _config.inverted);
  }
  markDeactivation();
}

bool SolenoidActuator::checkTimeout(uint32_t nowUs) {
  if (!_active) return false;

  uint32_t elapsed = nowUs - _activationStartUs;

  // HOLD behavior: transition from activation PWM to hold PWM after paramDefault ms
  if (_config.behavior == ActuatorBehavior::SOLENOID_HOLD && !_holdTransitioned && _active) {
    uint32_t transitionUs = (uint32_t)_config.paramDefault * 1000;
    if (elapsed >= transitionUs) {
      uint16_t holdPwm = map(_config.paramMax, 0, 255, 0, 4095);
      _driver->pwmWrite(_config.hwPin, holdPwm);
      _holdTransitioned = true;
      DBGF("[Actuator] Solenoid HOLD '%s' transitioned to hold PWM (%d)\n",
           _config.name, _config.paramMax);
    }
  }

  // MUTE behavior: no safety timeout (holds position indefinitely like servo mute)
  if (_config.behavior == ActuatorBehavior::SOLENOID_MUTE) {
    return false;
  }

  // HOLD behavior: use configured max active time (cooldownUs stores maxActiveTime*10)
  // STRIKE behavior: use strike duration (C1 fix), fallback to safety limit
  uint32_t maxOnUs;
  if (_config.behavior == ActuatorBehavior::SOLENOID_HOLD && _config.cooldownUs > 0) {
    maxOnUs = (uint32_t)_config.cooldownUs * 100;  // cooldownUs stored as value/100
  } else if (_strikeDurationUs > 0) {
    maxOnUs = _strikeDurationUs;  // C1: Use strike duration instead of 500ms safety max
  } else {
    maxOnUs = SOLENOID_MAX_ON_US;
  }

  if (elapsed >= maxOnUs) {
    DBGF("[Safety] Solenoid '%s' timeout after %lu us - forced stop\n",
         _config.name, (unsigned long)elapsed);
    stop();
    return true;
  }
  return false;
}
