#include "motor_actuator.h"

// --- Static member initialization ---
volatile uint32_t MotorActuator::_isrEdgeCount = 0;
MotorActuator* MotorActuator::_isrInstance = nullptr;
portMUX_TYPE MotorActuator::_isrMux = portMUX_INITIALIZER_UNLOCKED;

MotorActuator::MotorActuator(HalDriver* driver)
  : _driver(driver), _currentSpeed(0), _sensorPin(0),
    _targetEdges(0), _positionTracking(false) {}

void MotorActuator::init() {
  if (!_driver || !_driver->isReady()) return;
  _driver->pwmWrite(_config.hwPin, 0);
  _currentSpeed = 0;
  _active = false;
  _targetEdges = 0;
  _positionTracking = false;

  // Store sensor pin and configure as INPUT_PULLUP for interrupt use
  _sensorPin = _config.hwAddress;
  pinMode(_sensorPin, INPUT_PULLUP);

  DBGF("[Actuator] Motor '%s' init (ch=%d, sensor pin=%d, range=%d-%d)\n",
       _config.name, _config.hwPin, _sensorPin, _config.paramMin, _config.paramMax);
}

void MotorActuator::execute(const ActuatorCommand& cmd) {
  if (!_driver || !_driver->isReady() || !_config.enabled) return;

  uint32_t now = micros();

  switch ((CommandType)cmd.command_type) {
    case CommandType::PWM: {
      uint16_t cmdVal = _config.inverted ? (127 - cmd.value) : cmd.value;
      uint16_t speed = map(cmdVal, 0, 127, _config.paramMin, _config.paramMax);
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
      _positionTracking = (_targetEdges > 0);

      // Arm the hardware interrupt for optical edge counting
      _attachOpticalISR();

      uint16_t drive = (_config.paramDefault == 0) ? 128 : _config.paramDefault;
      if (drive > 255) drive = 255;
      _driver->pwmWrite(_config.hwPin, drive);
      _currentSpeed = drive;
      markActivation(now);
      DBGF("[Actuator] Motor optical '%s' target edges=%lu (sensor pin=%d, ISR armed)\n",
           _config.name, (unsigned long)_targetEdges, _sensorPin);
      break;
    }

    case CommandType::OFF:
      stop();
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// ISR - called on RISING edge of the optical sensor
// Must be in IRAM, minimal work only (increment counter)
// ---------------------------------------------------------------------------
void IRAM_ATTR MotorActuator::_opticalISR() {
  portENTER_CRITICAL_ISR(&_isrMux);
  _isrEdgeCount++;
  portEXIT_CRITICAL_ISR(&_isrMux);
}

// ---------------------------------------------------------------------------
// Attach / Detach helpers
// ---------------------------------------------------------------------------
void MotorActuator::_attachOpticalISR() {
  // Reset edge count atomically
  portENTER_CRITICAL(&_isrMux);
  _isrEdgeCount = 0;
  portEXIT_CRITICAL(&_isrMux);

  _isrInstance = this;
  attachInterrupt(digitalPinToInterrupt(_sensorPin), _opticalISR, RISING);
}

void MotorActuator::_detachOpticalISR() {
  detachInterrupt(digitalPinToInterrupt(_sensorPin));
  _isrInstance = nullptr;
}

// ---------------------------------------------------------------------------
// Optical tracking - reads ISR edge count, checks target, enforces timeout
// ---------------------------------------------------------------------------
void MotorActuator::_updateOpticalTracking(uint32_t nowUs) {
  if (!_positionTracking || _targetEdges == 0 || !_active) return;

  // Atomically read the edge count from the ISR
  uint32_t edges;
  portENTER_CRITICAL(&_isrMux);
  edges = _isrEdgeCount;
  portEXIT_CRITICAL(&_isrMux);

  if (edges >= _targetEdges) {
    DBGF("[Actuator] Motor optical '%s' reached target (%lu edges)\n",
         _config.name, (unsigned long)edges);
    stop();
    return;
  }

  // Safety against stalled sensor while tracking
  uint32_t elapsed = nowUs - _activationStartUs;
  if (elapsed >= MOTOR_MAX_CONTINUOUS_US) {
    DBGF("[Safety] Motor optical '%s' timeout waiting for sensor (%lu/%lu edges)\n",
         _config.name, (unsigned long)edges, (unsigned long)_targetEdges);
    stop();
  }
}

void MotorActuator::stop() {
  if (!_driver || !_driver->isReady()) return;

  // Detach ISR before stopping the motor to avoid spurious counts
  _detachOpticalISR();

  _driver->pwmWrite(_config.hwPin, 0);
  _currentSpeed = 0;
  _positionTracking = false;
  _targetEdges = 0;
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
