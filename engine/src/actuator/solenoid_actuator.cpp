#include "solenoid_actuator.h"

SolenoidActuator::SolenoidActuator(HalDriver* driver)
  : _driver(driver) {}

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
      // Verifier cooldown avant activation
      if (!checkCooldown(now)) {
        return; // Encore en cooldown, ignorer la commande
      }

      // Activation: ecrire HIGH (ou PWM pour solenoid hold)
      if (_config.behavior == ActuatorBehavior::SOLENOID_HOLD) {
        // PWM proportionnel a la valeur
        uint16_t pwmVal = map(cmd.value, 0, 127, 0, 4095);
        _driver->pwmWrite(_config.hwPin, pwmVal);
      } else {
        _driver->digitalWrite(_config.hwPin, !_config.inverted);
      }
      markActivation(now);
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
  } else {
    _driver->digitalWrite(_config.hwPin, _config.inverted);
  }
  markDeactivation();
}

bool SolenoidActuator::checkTimeout(uint32_t nowUs) {
  if (!_active) return false;

  uint32_t elapsed = nowUs - _activationStartUs;
  if (elapsed >= SOLENOID_MAX_ON_US) {
    DBGF("[Safety] Solenoid '%s' timeout after %lu us - forced stop\n",
         _config.name, (unsigned long)elapsed);
    stop();
    return true;
  }
  return false;
}
