#include "servo_actuator.h"

ServoActuator::ServoActuator(PCA9685Driver* driver)
  : _driver(driver), _currentAngle(0) {}

void ServoActuator::init() {
  if (!_driver || !_driver->isReady()) return;
  _currentAngle = _config.paramDefault;
  _moveToAngle(_currentAngle);
  _combBrushRunning = false;
  _lastCombBrushStepUs = micros();
  DBGF("[Actuator] Servo '%s' init (ch=%d, range=%d-%d, default=%d)\n",
       _config.name, _config.hwPin, _config.paramMin, _config.paramMax, _config.paramDefault);
}

void ServoActuator::execute(const ActuatorCommand& cmd) {
  if (!_driver || !_driver->isReady() || !_config.enabled) return;

  uint32_t now = micros();

  switch ((CommandType)cmd.command_type) {
    case CommandType::POSITION: {
      uint8_t angle;
      if (_config.behavior == ActuatorBehavior::PITCH_BEND) {
        int16_t center = (_config.paramMin + _config.paramMax) / 2;
        int16_t half = (_config.paramMax - _config.paramMin) / 2;
        int16_t bend = map(cmd.value, 0, 127, -half, half);
        int16_t target = center + bend;
        if (target < _config.paramMin) target = _config.paramMin;
        if (target > _config.paramMax) target = _config.paramMax;
        angle = (uint8_t)target;
      } else {
        angle = map(cmd.value, 0, 127, _config.paramMin, _config.paramMax);
      }
      _combBrushRunning = false;
      _moveToAngle(angle);
      markActivation(now);
      break;
    }

    case CommandType::PULSE: {
      uint8_t strikeAngle = map(cmd.value, 0, 127, _config.paramMin, _config.paramMax);
      _combBrushRunning = false;
      _moveToAngle(strikeAngle);
      markActivation(now);
      break;
    }

    case CommandType::PWM: {
      if (_config.behavior == ActuatorBehavior::COMB_BRUSH ||
          _config.behavior == ActuatorBehavior::SERVO_STRIKE) {
        // cmd.value controls oscillation speed: 0 stop, 127 fast
        if (cmd.value == 0) {
          _combBrushRunning = false;
          stop();
          break;
        }

        // period 30ms..250ms depending on speed
        _combBrushPeriodUs = map(cmd.value, 1, 127, 250000, 30000);
        if (_combBrushPeriodUs < 30000) _combBrushPeriodUs = 30000;
        _combBrushRunning = true;
        _combBrushHigh = !_combBrushHigh;
        _lastCombBrushStepUs = now;
        _moveToAngle(_combBrushHigh ? _config.paramMax : _config.paramMin);
        markActivation(now);
      }
      break;
    }

    case CommandType::OFF:
      _combBrushRunning = false;
      stop();
      break;

    default:
      break;
  }
}

void ServoActuator::stop() {
  if (!_driver || !_driver->isReady()) return;
  _combBrushRunning = false;
  _moveToAngle(_config.paramDefault);
  markDeactivation();
}

bool ServoActuator::checkTimeout(uint32_t nowUs) {
  if (_combBrushRunning) {
    if (nowUs - _lastCombBrushStepUs >= _combBrushPeriodUs) {
      _combBrushHigh = !_combBrushHigh;
      _lastCombBrushStepUs = nowUs;
      _moveToAngle(_combBrushHigh ? _config.paramMax : _config.paramMin);
      return false;
    }
  }

  if (_active) {
    uint32_t elapsed = nowUs - _activationStartUs;
    if (elapsed >= SOLENOID_MAX_ON_US) {
      stop();
      return true;
    }
  }
  return false;
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
