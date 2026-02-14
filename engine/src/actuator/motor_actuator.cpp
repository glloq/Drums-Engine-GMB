#include "motor_actuator.h"

MotorActuator::MotorActuator(HalDriver* driver)
  : _driver(driver), _currentSpeed(0) {}

void MotorActuator::init() {
  if (!_driver || !_driver->isReady()) return;
  _driver->pwmWrite(_config.hwPin, 0);
  _currentSpeed = 0;
  DBGF("[Actuator] Motor '%s' init (ch=%d, range=%d-%d)\n",
       _config.name, _config.hwPin, _config.paramMin, _config.paramMax);
}

void MotorActuator::execute(const ActuatorCommand& cmd) {
  if (!_driver || !_driver->isReady() || !_config.enabled) return;

  switch ((CommandType)cmd.command_type) {
    case CommandType::PWM: {
      uint16_t speed = map(cmd.value, 0, 127, _config.paramMin, _config.paramMax);
      _driver->pwmWrite(_config.hwPin, speed);
      _currentSpeed = speed;
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
}
