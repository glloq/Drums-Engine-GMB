#include "motor_actuator.h"

// --- Static member initialization ---
MotorActuator::IsrSlot MotorActuator::_isrSlots[MAX_OPTICAL_MOTORS] = {
  {0, nullptr, portMUX_INITIALIZER_UNLOCKED},
  {0, nullptr, portMUX_INITIALIZER_UNLOCKED},
  {0, nullptr, portMUX_INITIALIZER_UNLOCKED},
  {0, nullptr, portMUX_INITIALIZER_UNLOCKED}
};
uint8_t MotorActuator::_isrSlotPins[MAX_OPTICAL_MOTORS] = {0xFF, 0xFF, 0xFF, 0xFF};
constexpr void (*MotorActuator::_isrTable[MAX_OPTICAL_MOTORS])();

MotorActuator::MotorActuator(HalDriver* driver)
  : _driver(driver), _currentSpeed(0),
    _sensorPin(0xFF), _dirPin(0xFF), _forward(true),
    _targetEdges(0), _positionTracking(false),
    _timedDurationUs(0), _alternateRunning(false),
    _homing(false), _endStopReached(false) {}

void MotorActuator::init() {
  if (!_driver || !_driver->isReady()) return;
  _driver->pwmWrite(_config.hwPin, 0);
  _currentSpeed = 0;
  _active = false;
  _targetEdges = 0;
  _positionTracking = false;
  _timedDurationUs = 0;
  _alternateRunning = false;
  _sensorPin = 0xFF;
  _dirPin = 0xFF;
  _forward = true;

  // hwAddress : role depend du type/behavior
  //   MOTOR_OPTICAL   → capteur optique (INPUT_PULLUP, ISR)
  //   MOTOR_ALTERNATE → pin direction H-bridge (OUTPUT)
  //   Autres PWM_MOTOR → ignore
  if (_config.type == ActuatorType::MOTOR_OPTICAL) {
    _sensorPin = _config.hwAddress;
    if (_sensorPin > 0 && _sensorPin < 40) {
      pinMode(_sensorPin, INPUT_PULLUP);
    }
  } else if (_config.behavior == ActuatorBehavior::MOTOR_ALTERNATE) {
    if (_config.hwAddress > 0 && _config.hwAddress < 40) {
      _dirPin = _config.hwAddress;
      pinMode(_dirPin, OUTPUT);
      digitalWrite(_dirPin, HIGH);
    }
  }

  // End stop pins
  if (_config.endStopPin1 != 0xFF && _config.endStopPin1 < 40) {
    pinMode(_config.endStopPin1, INPUT_PULLUP);
  }
  if (_config.endStopPin2 != 0xFF && _config.endStopPin2 < 40) {
    pinMode(_config.endStopPin2, INPUT_PULLUP);
  }
  _homing = false;
  _endStopReached = false;

  DBGF("[Actuator] Motor '%s' init (ch=%d, behavior=%d, endstop1=%d, endstop2=%d, range=%d-%d)\n",
       _config.name, _config.hwPin, (int)_config.behavior,
       _config.endStopPin1, _config.endStopPin2,
       _config.paramMin, _config.paramMax);
}

// =========================================================================
// execute — dispatch selon CommandType et behavior
// =========================================================================
void MotorActuator::execute(const ActuatorCommand& cmd) {
  if (!_driver || !_driver->isReady() || !_config.enabled) return;

  uint32_t now = micros();

  switch ((CommandType)cmd.command_type) {

    // -----------------------------------------------------------------
    // PULSE — declenchement ponctuel (TIMED, SWEEP, ALTERNATE)
    // -----------------------------------------------------------------
    case CommandType::PULSE: {
      // Arreter tout tracking optique en cours
      if (_positionTracking) _detachOpticalISR();
      _positionTracking = false;

      uint16_t cmdVal = _config.inverted ? (127 - cmd.value) : cmd.value;
      uint16_t speed = map(cmdVal, 0, 127, _config.paramMin, _config.paramMax);

      switch (_config.behavior) {

        case ActuatorBehavior::MOTOR_TIMED:
          // Tourne a 'speed' pendant 'duration' puis arret auto
          _timedDurationUs = (uint32_t)cmd.duration * 100;
          _alternateRunning = false;
          _driver->pwmWrite(_config.hwPin, speed);
          _currentSpeed = speed;
          markActivation(now);
          DBGF("[Actuator] Motor '%s' timed (speed=%d, dur=%lu us)\n",
               _config.name, speed, (unsigned long)_timedDurationUs);
          break;

        case ActuatorBehavior::MOTOR_SWEEP:
          // Tourne a 'speed' jusqu'a ce qu'un end stop soit atteint
          _endStopReached = false;
          _alternateRunning = false;
          _driver->pwmWrite(_config.hwPin, speed);
          _currentSpeed = speed;
          markActivation(now);
          DBGF("[Actuator] Motor '%s' sweep (speed=%d)\n",
               _config.name, speed);
          break;

        case ActuatorBehavior::MOTOR_ALTERNATE:
          // Alterne : tourne a 'speed', inverse direction a chaque end stop
          _forward = true;
          _alternateRunning = true;
          _endStopReached = false;
          _setDirection(_forward);
          _driver->pwmWrite(_config.hwPin, speed);
          _currentSpeed = speed;
          markActivation(now);
          DBGF("[Actuator] Motor '%s' alternate start (speed=%d)\n",
               _config.name, speed);
          break;

        default:
          break;
      }
      break;
    }

    // -----------------------------------------------------------------
    // PWM — controle direct de vitesse (tous behaviors moteur)
    // -----------------------------------------------------------------
    case CommandType::PWM: {
      if (_positionTracking) _detachOpticalISR();
      _positionTracking = false;
      _targetEdges = 0;

      uint16_t cmdVal = _config.inverted ? (127 - cmd.value) : cmd.value;
      uint16_t speed = map(cmdVal, 0, 127, _config.paramMin, _config.paramMax);
      _driver->pwmWrite(_config.hwPin, speed);
      _currentSpeed = speed;

      if (speed > 0) {
        if (!_active) markActivation(now);
      } else {
        _alternateRunning = false;
        markDeactivation();
      }
      break;
    }

    // -----------------------------------------------------------------
    // POSITION — suivi optique (MOTOR_OPTICAL_TRACK uniquement)
    // -----------------------------------------------------------------
    case CommandType::POSITION: {
      if (_config.behavior != ActuatorBehavior::MOTOR_OPTICAL_TRACK) break;

      uint16_t minEdges = (_config.paramMin == 0) ? 1 : _config.paramMin;
      uint16_t maxEdges = (_config.paramMax < minEdges) ? minEdges : _config.paramMax;
      _targetEdges = map(cmd.value, 0, 127, minEdges, maxEdges);
      _positionTracking = (_targetEdges > 0);

      _attachOpticalISR();

      uint16_t drive = (_config.paramDefault == 0) ? 128 : _config.paramDefault;
      if (drive > 255) drive = 255;
      _driver->pwmWrite(_config.hwPin, drive);
      _currentSpeed = drive;
      markActivation(now);
      DBGF("[Actuator] Motor optical '%s' target=%lu edges (sensor=%d)\n",
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

// =========================================================================
// Direction — ecriture GPIO pour H-bridge (DIR+PWM)
// =========================================================================
void MotorActuator::_setDirection(bool forward) {
  if (_dirPin != 0xFF && _dirPin < 40) {
    digitalWrite(_dirPin, forward ? HIGH : LOW);
  }
}

// ---------------------------------------------------------------------------
// ISR trampolines - one per slot, each increments its own counter
// ---------------------------------------------------------------------------
#define MAKE_ISR(N) \
  void IRAM_ATTR MotorActuator::_opticalISR##N() { \
    portENTER_CRITICAL_ISR(&_isrSlots[N].mux); \
    _isrSlots[N].edgeCount++; \
    portEXIT_CRITICAL_ISR(&_isrSlots[N].mux); \
  }
MAKE_ISR(0) MAKE_ISR(1) MAKE_ISR(2) MAKE_ISR(3)
#undef MAKE_ISR

// ---------------------------------------------------------------------------
// Attach / Detach helpers (per-instance slot allocation)
// ---------------------------------------------------------------------------
void MotorActuator::_attachOpticalISR() {
  // Already attached?
  if (_isrSlotIdx >= 0) {
    portENTER_CRITICAL(&_isrSlots[_isrSlotIdx].mux);
    _isrSlots[_isrSlotIdx].edgeCount = 0;
    portEXIT_CRITICAL(&_isrSlots[_isrSlotIdx].mux);
    return;
  }
  // Find a free slot (or reuse one already bound to our pin)
  int8_t freeSlot = -1;
  for (uint8_t i = 0; i < MAX_OPTICAL_MOTORS; i++) {
    if (_isrSlotPins[i] == _sensorPin) { freeSlot = i; break; }
    if (freeSlot < 0 && _isrSlotPins[i] == 0xFF) freeSlot = i;
  }
  if (freeSlot < 0) {
    DBGF("[Safety] Motor '%s' - no free ISR slot for optical sensor!\n", _config.name);
    return;
  }
  _isrSlotIdx = freeSlot;
  _isrSlotPins[freeSlot] = _sensorPin;
  _isrSlots[freeSlot].instance = this;

  portENTER_CRITICAL(&_isrSlots[freeSlot].mux);
  _isrSlots[freeSlot].edgeCount = 0;
  portEXIT_CRITICAL(&_isrSlots[freeSlot].mux);

  attachInterrupt(digitalPinToInterrupt(_sensorPin), _isrTable[freeSlot], RISING);
}

void MotorActuator::_detachOpticalISR() {
  if (_isrSlotIdx < 0) return;
  detachInterrupt(digitalPinToInterrupt(_sensorPin));
  _isrSlots[_isrSlotIdx].instance = nullptr;
  _isrSlotPins[_isrSlotIdx] = 0xFF;
  _isrSlotIdx = -1;
}

// ---------------------------------------------------------------------------
// Optical tracking - reads ISR edge count, checks target, enforces timeout
// ---------------------------------------------------------------------------
void MotorActuator::_updateOpticalTracking(uint32_t nowUs) {
  if (!_positionTracking || _targetEdges == 0 || !_active || _isrSlotIdx < 0) return;

  // Atomically read the edge count from our ISR slot
  uint32_t edges;
  portENTER_CRITICAL(&_isrSlots[_isrSlotIdx].mux);
  edges = _isrSlots[_isrSlotIdx].edgeCount;
  portEXIT_CRITICAL(&_isrSlots[_isrSlotIdx].mux);

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

// ---------------------------------------------------------------------------
// End stop checking - reads digital pins, returns true if any triggered (LOW)
// ---------------------------------------------------------------------------
bool MotorActuator::_checkEndStops() {
  if (_config.endStopPin1 != 0xFF && _config.endStopPin1 < 40) {
    if (digitalRead(_config.endStopPin1) == LOW) {
      return true;
    }
  }
  if (_config.endStopPin2 != 0xFF && _config.endStopPin2 < 40) {
    if (digitalRead(_config.endStopPin2) == LOW) {
      return true;
    }
  }
  return false;
}

void MotorActuator::stop() {
  if (!_driver || !_driver->isReady()) return;

  _detachOpticalISR();

  _driver->pwmWrite(_config.hwPin, 0);
  _currentSpeed = 0;
  _positionTracking = false;
  _targetEdges = 0;
  _timedDurationUs = 0;
  _alternateRunning = false;
  _homing = false;
  markDeactivation();
}

// =========================================================================
// checkTimeout — watchdog appele a 1 kHz par ActuatorManager
// =========================================================================
// Logique par behavior :
//   MOTOR_TIMED     → arret apres _timedDurationUs
//   MOTOR_SWEEP     → arret quand end stop atteint (check standard)
//   MOTOR_ALTERNATE → inverse direction quand end stop atteint, continue
//   MOTOR_SPEED     → timeout securite seulement
//   OPTICAL_TRACK   → suivi compteur ISR
// =========================================================================
bool MotorActuator::checkTimeout(uint32_t nowUs) {
  if (!_active) return false;

  // --- End stop (polling 1 kHz, front montant) ---
  if (_checkEndStops()) {
    if (!_endStopReached) {
      _endStopReached = true;

      // MOTOR_ALTERNATE : inverser et continuer
      if (_alternateRunning) {
        _driver->pwmWrite(_config.hwPin, 0);           // pause breve
        _forward = !_forward;
        _setDirection(_forward);
        _driver->pwmWrite(_config.hwPin, _currentSpeed); // reprendre
        _activationStartUs = nowUs;                     // reset timeout securite
        DBGF("[Actuator] Motor '%s' end stop -> %s\n",
             _config.name, _forward ? "FWD" : "REV");
        return false;
      }

      // Tous les autres behaviors : arret
      DBGF("[Actuator] Motor '%s' end stop triggered - stopping\n", _config.name);
      stop();
      return true;
    }
  } else {
    _endStopReached = false;
  }

  // --- MOTOR_OPTICAL_TRACK : suivi compteur ISR ---
  if (_config.behavior == ActuatorBehavior::MOTOR_OPTICAL_TRACK) {
    _updateOpticalTracking(nowUs);
    return !_active;
  }

  // --- MOTOR_TIMED : arret apres duree ecoulee ---
  if (_config.behavior == ActuatorBehavior::MOTOR_TIMED && _timedDurationUs > 0) {
    uint32_t elapsed = nowUs - _activationStartUs;
    if (elapsed >= _timedDurationUs) {
      DBGF("[Actuator] Motor '%s' timed done (%lu us)\n",
           _config.name, (unsigned long)_timedDurationUs);
      stop();
      return true;
    }
  }

  // --- Timeout securite global ---
  uint32_t elapsed = nowUs - _activationStartUs;
  if (elapsed >= MOTOR_MAX_CONTINUOUS_US) {
    DBGF("[Safety] Motor '%s' timeout %lu us - forced stop\n",
         _config.name, (unsigned long)elapsed);
    stop();
    return true;
  }

  return false;
}
