#include "motor_actuator.h"

MotorActuator::MotorActuator(HalDriver* driver)
  : _driver(driver), _currentSpeed(0) {}

void MotorActuator::init() {
  if (!_driver || !_driver->isReady()) return;
  _driver->pwmWrite(_config.hwPin, 0);
  _currentSpeed = 0;
  _active = false;
  DBGF("[Actuator] Motor '%s' init (ch=%d, range=%d-%d)\n",
       _config.name, _config.hwPin, _config.paramMin, _config.paramMax);
}

void MotorActuator::execute(const ActuatorCommand& cmd) {
  if (!_driver || !_driver->isReady() || !_config.enabled) return;

  uint32_t now = micros();

  switch ((CommandType)cmd.command_type) {
    case CommandType::PWM: {
      uint16_t speed = map(cmd.value, 0, 127, _config.paramMin, _config.paramMax);
      _driver->pwmWrite(_config.hwPin, speed);
      _currentSpeed = speed;
      if (speed > 0) {
        if (!_active) {
          markActivation(now);
        }
      } else {
        markDeactivation();
      }
      break;
    }

    case CommandType::OFF:
      stop();
      break;

    default:
      break;
  }
}

void MotorActuator::stop() {
  if (!_driver || !_driver->isReady()) return;
  _driver->pwmWrite(_config.hwPin, 0);
  _currentSpeed = 0;
  markDeactivation();
}

bool MotorActuator::checkTimeout(uint32_t nowUs) {
  if (!_active) return false;

  uint32_t elapsed = nowUs - _activationStartUs;
  if (elapsed >= MOTOR_MAX_CONTINUOUS_US) {
    DBGF("[Safety] Motor '%s' duty-cycle timeout after %lu us - forced stop\n",
         _config.name, (unsigned long)elapsed);
    stop();
    return true;
  }
  return false;
}
