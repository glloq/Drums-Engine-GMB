#include "servo_actuator.h"

ServoActuator::ServoActuator(PCA9685Driver* driver)
  : _driver(driver), _currentAngle(0) {}

void ServoActuator::init() {
  if (!_driver || !_driver->isReady()) return;
  _currentAngle = _config.paramDefault;
  _moveToAngle(_currentAngle);
  DBGF("[Actuator] Servo '%s' init (ch=%d, range=%d-%d, default=%d)\n",
       _config.name, _config.hwPin, _config.paramMin, _config.paramMax, _config.paramDefault);
}

void ServoActuator::execute(const ActuatorCommand& cmd) {
  if (!_driver || !_driver->isReady() || !_config.enabled) return;

  switch ((CommandType)cmd.command_type) {
    case CommandType::POSITION: {
      // Mapper la valeur (0-127) vers la plage d'angle
      uint8_t angle = map(cmd.value, 0, 127, _config.paramMin, _config.paramMax);
      _moveToAngle(angle);
      break;
    }

    case CommandType::PULSE: {
      // Frappe servo: aller a l'angle puis revenir
      uint8_t strikeAngle = map(cmd.value, 0, 127, _config.paramMin, _config.paramMax);
      _moveToAngle(strikeAngle);
      // Le retour sera gere par une commande OFF schedulee
      break;
    }

    case CommandType::OFF:
      stop();
      break;

    default:
      break;
  }
}

void ServoActuator::stop() {
  if (!_driver || !_driver->isReady()) return;
  _moveToAngle(_config.paramDefault);
}

void ServoActuator::_moveToAngle(uint8_t angle) {
  if (angle < _config.paramMin) angle = _config.paramMin;
  if (angle > _config.paramMax) angle = _config.paramMax;
  if (_config.inverted) {
    angle = _config.paramMax - (angle - _config.paramMin);
  }
  _currentAngle = angle;
  _driver->setServoPulse(_config.hwPin, _driver->angleToPulse(angle));
}
