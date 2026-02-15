#include "motor_actuator.h"

MotorActuator::MotorActuator(HalDriver* driver)
  : _driver(driver), _currentSpeed(0),
    _lastSensorState(false), _edgeCount(0), _targetEdges(0), _positionTracking(false) {}

void MotorActuator::init() {
  if (!_driver || !_driver->isReady()) return;
  _driver->pwmWrite(_config.hwPin, 0);
  _currentSpeed = 0;
  _active = false;
  _edgeCount = 0;
  _targetEdges = 0;
  _positionTracking = false;
  _lastSensorState = _driver->digitalRead(_config.hwAddress);
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
      _positionTracking = false;
      _targetEdges = 0;
      if (speed > 0) {
        if (!_active) {
          markActivation(now);
        }
      } else {
        markDeactivation();
      }
      break;
    }

    case CommandType::POSITION: {
      if (_config.behavior != ActuatorBehavior::MOTOR_OPTICAL_TRACK) {
        break;
      }

      uint16_t minEdges = (_config.paramMin == 0) ? 1 : _config.paramMin;
      uint16_t maxEdges = (_config.paramMax < minEdges) ? minEdges : _config.paramMax;
      _targetEdges = map(cmd.value, 0, 127, minEdges, maxEdges);
      _edgeCount = 0;
      _positionTracking = (_targetEdges > 0);
      _lastSensorState = _driver->digitalRead(_config.hwAddress);

      uint16_t drive = (_config.paramDefault == 0) ? 128 : _config.paramDefault;
      if (drive > 255) drive = 255;
      _driver->pwmWrite(_config.hwPin, drive);
      _currentSpeed = drive;
      markActivation(now);
      DBGF("[Actuator] Motor optical '%s' target edges=%lu (sensor pin=%d)\n",
           _config.name, (unsigned long)_targetEdges, _config.hwAddress);
      break;
    }

    case CommandType::OFF:
      stop();
      break;

    default:
      break;
  }
}

void MotorActuator::_updateOpticalTracking(uint32_t nowUs) {
  if (!_positionTracking || _targetEdges == 0 || !_active) return;

  bool sensor = _driver->digitalRead(_config.hwAddress);
  if (!_lastSensorState && sensor) {
    _edgeCount++;
    if (_edgeCount >= _targetEdges) {
      DBGF("[Actuator] Motor optical '%s' reached target (%lu)\n",
           _config.name, (unsigned long)_edgeCount);
      stop();
      return;
    }
  }
  _lastSensorState = sensor;

  // safety against stalled sensor while tracking
  uint32_t elapsed = nowUs - _activationStartUs;
  if (elapsed >= MOTOR_MAX_CONTINUOUS_US) {
    DBGF("[Safety] Motor optical '%s' timeout waiting for sensor (%lu edges)\n",
         _config.name, (unsigned long)_edgeCount);
    stop();
  }
}

void MotorActuator::stop() {
  if (!_driver || !_driver->isReady()) return;
  _driver->pwmWrite(_config.hwPin, 0);
  _currentSpeed = 0;
  _positionTracking = false;
  _targetEdges = 0;
  _edgeCount = 0;
  markDeactivation();
}

bool MotorActuator::checkTimeout(uint32_t nowUs) {
  if (!_active) return false;

  if (_config.behavior == ActuatorBehavior::MOTOR_OPTICAL_TRACK) {
    _updateOpticalTracking(nowUs);
    return !_active;
  }

  uint32_t elapsed = nowUs - _activationStartUs;
  if (elapsed >= MOTOR_MAX_CONTINUOUS_US) {
    DBGF("[Safety] Motor '%s' duty-cycle timeout after %lu us - forced stop\n",
         _config.name, (unsigned long)elapsed);
    stop();
    return true;
  }
  return false;
}
