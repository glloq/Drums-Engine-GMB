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
      } else if (_config.behavior == ActuatorBehavior::HIHAT_CONTROLLER) {
        // CC#4 hi-hat control: 0=open (paramMin), 127=closed (paramMax)
        _lastCcValue = cmd.value;
        angle = map(cmd.value, 0, 127, _config.paramMin, _config.paramMax);
        _splashActive = false;  // Cancel any active splash on new CC
      } else if (_config.behavior == ActuatorBehavior::SERVO_MUTE) {
        // Mute: 0 = position mute (paramMin), 127 = position libre (paramMax)
        angle = (cmd.value < 64) ? _config.paramMin : _config.paramMax;
      } else {
        angle = map(cmd.value, 0, 127, _config.paramMin, _config.paramMax);
      }
      _combBrushRunning = false;
      _moveToAngle(angle);
      markActivation(now);
      break;
    }

    case CommandType::PULSE: {
      if (_config.behavior == ActuatorBehavior::HIHAT_CONTROLLER) {
        // Splash: quickly open then return to CC-derived position
        // Use _lastCcValue to compute return angle (not _currentAngle which may
        // already be at paramMin if we're re-splashing during an active splash)
        _splashReturnAngle = map(_lastCcValue, 0, 127, _config.paramMin, _config.paramMax);
        _splashActive = true;
        _splashStartUs = now;
        _moveToAngle(_config.paramMin);  // Open position
        markActivation(now);
        break;
      }
      if (_config.behavior == ActuatorBehavior::SERVO_MUTE) {
        // Toggle mute: if currently at mute position go to free, and vice versa
        uint8_t muteAngle = _config.paramMin;
        uint8_t freeAngle = _config.paramMax;
        uint8_t target = (_currentAngle <= (muteAngle + freeAngle) / 2)
                         ? freeAngle : muteAngle;
        _moveToAngle(target);
        markActivation(now);
        break;
      }
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

  // Hi-hat splash return
  if (_splashActive && (nowUs - _splashStartUs >= SPLASH_DURATION_US)) {
    _splashActive = false;
    _moveToAngle(_splashReturnAngle);
    return false;
  }

  if (_active) {
    // Position-holding behaviors (hi-hat pedal, mute, pitch bend, servo position)
    // must stay at their set angle indefinitely — no timeout.
    // Only oscillating / strike behaviors get a safety timeout.
    if (_config.behavior == ActuatorBehavior::HIHAT_CONTROLLER ||
        _config.behavior == ActuatorBehavior::SERVO_MUTE ||
        _config.behavior == ActuatorBehavior::PITCH_BEND ||
        _config.behavior == ActuatorBehavior::SERVO_POSITION) {
      return false;
    }
    uint32_t elapsed = nowUs - _activationStartUs;
    if (elapsed >= SOLENOID_MAX_ON_US) {
      DBGF("[Safety] Servo '%s' timeout after %lu us - forced stop\n",
           _config.name, (unsigned long)elapsed);
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
