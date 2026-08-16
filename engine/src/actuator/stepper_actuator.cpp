#include "stepper_actuator.h"
#include <math.h>

StepperActuator::StepperActuator(GpioDriver* gpio) : StepperActuator() {
  _gpio = gpio;
}

void StepperActuator::init() {
  if (!_gpio) return;

  _gpio->configureOutput(_config.hwPin);            // STEP
  _gpio->configureOutput(_config.hwAddress);        // DIR
  if (_config.enablePin != 0xFF) {
    _gpio->configureOutput(_config.enablePin);
  }
  if (_config.endStopPin1 != 0xFF) pinMode(_config.endStopPin1, INPUT_PULLUP);
  if (_config.endStopPin2 != 0xFF) pinMode(_config.endStopPin2, INPUT_PULLUP);

  _setEnabled(false);
  _currentSteps = (int32_t)_config.paramDefault;
  _targetSteps = _currentSteps;
  _currentSps = 0;
  _targetSps = 0;
  _rotating = false;
  _homing = false;
  _returnAfterMove = false;
  // Sans butee d'origine, on ne peut que FAIRE CONFIANCE a la position de
  // repos declaree : le mecanisme est cense etre laisse au repos a l'arret.
  // Avec une butee, la position reste inconnue jusqu'a la prise d'origine.
  _homed = (_config.endStopPin1 == 0xFF);
  _active = false;

  DBGF("[Actuator] Stepper '%s' init (step=%d dir=%d en=%d, %d..%d steps, %d sps)\n",
       _config.name, _config.hwPin, _config.hwAddress, _config.enablePin,
       (int)_minSteps(), (int)_maxSteps(), _maxSps());
}

void StepperActuator::execute(const ActuatorCommand& cmd) {
  if (!_gpio || !_config.enabled) return;

  uint32_t now = micros();

  switch ((CommandType)cmd.command_type) {
    case CommandType::OFF:
      stop();
      return;

    case CommandType::PWM: {
      // Rotation continue. La valeur 0..127 est mappee sur [paramMin, paramMax]
      // exprimes en % de la vitesse max ; 0 arrete le moteur.
      if (_config.behavior != ActuatorBehavior::STEPPER_ROTATE) return;
      uint8_t value = cmd.value > 127 ? 127 : (uint8_t)cmd.value;
      if (value == 0) {
        stop();
        return;
      }
      uint16_t pct = (uint16_t)map(value, 1, 127,
                                   (long)_config.paramMin, (long)_config.paramMax);
      if (pct > 100) pct = 100;
      uint32_t sps = ((uint32_t)_maxSps() * pct) / 100;
      if (sps == 0) {
        stop();
        return;
      }
      if (!checkCooldown(now)) return;
      _rotating = true;
      _returnAfterMove = false;
      _targetSps = (uint16_t)sps;
      _setDirection(!_config.inverted);
      _setEnabled(true);
      markActivation(now);
      _lastStepUs = now;
      _lastServiceUs = now;
      return;
    }

    case CommandType::POSITION: {
      if (_config.behavior == ActuatorBehavior::STEPPER_ROTATE) return;
      if (!checkCooldown(now)) return;
      uint8_t value = cmd.value > 127 ? 127 : (uint8_t)cmd.value;
      int32_t target = map(value, 0, 127, _minSteps(), _maxSteps());
      _returnAfterMove = false;
      _beginMove(target, _maxSps());
      return;
    }

    case CommandType::PULSE: {
      // Frappe : aller a la position de frappe, puis retour automatique au
      // repos. La velocite pilote la vitesse d'attaque, pas la course.
      if (_config.behavior != ActuatorBehavior::STEPPER_STRIKE) return;
      if (!checkCooldown(now)) return;
      uint8_t value = cmd.value > 127 ? 127 : (uint8_t)cmd.value;
      // Plancher a 10 % pour qu'une velocite faible produise quand meme un
      // mouvement, au lieu d'une cible atteinte en plusieurs secondes.
      uint32_t sps = ((uint32_t)_maxSps() * (10 + ((uint32_t)value * 90) / 127)) / 100;
      if (sps == 0) sps = 1;
      _returnAfterMove = true;
      _returnTargetSteps = (int32_t)_config.paramDefault;
      _beginMove(_maxSteps(), (uint16_t)sps);
      return;
    }

    default:
      return;
  }
}

void StepperActuator::_beginMove(int32_t targetSteps, uint16_t sps) {
  if (targetSteps < _minSteps()) targetSteps = _minSteps();
  if (targetSteps > _maxSteps()) targetSteps = _maxSteps();

  // Prise d'origine implicite : tant que la position reelle est inconnue, tout
  // deplacement absolu commencerait a partir d'une hypothese fausse et pourrait
  // pousser le mecanisme dans sa butee. On rejoint d'abord l'origine.
  if (!_homed && _config.endStopPin1 != 0xFF) {
    uint32_t now = micros();
    _homing = true;
    _homingStartUs = now;
    _targetSps = _maxSps() / 4;   // approche lente de la butee
    if (_targetSps == 0) _targetSps = 1;
    _setDirection(false);
    _setEnabled(true);
    markActivation(now);
    _lastStepUs = now;
    _lastServiceUs = now;
    // La cible demandee est conservee et reprise a la fin de la prise d'origine.
    _targetSteps = targetSteps;
    DBGF("[Actuator] Stepper '%s' homing before absolute move\n", _config.name);
    return;
  }

  if (targetSteps == _currentSteps) {
    // Rien a parcourir. Un aller-retour de frappe deja en position de frappe
    // doit quand meme enchainer son retour au repos, sinon le drapeau resterait
    // arme sans mouvement.
    if (_returnAfterMove) _finishMove();
    return;
  }

  uint32_t now = micros();
  _rotating = false;
  _targetSteps = targetSteps;
  _targetSps = sps > _maxSps() ? _maxSps() : sps;
  if (_targetSps == 0) _targetSps = 1;
  _setDirection(_targetSteps > _currentSteps);
  _setEnabled(true);
  markActivation(now);
  _lastStepUs = now;
  _lastServiceUs = now;
}

void StepperActuator::_finishMove() {
  if (_returnAfterMove) {
    _returnAfterMove = false;
    int32_t back = _returnTargetSteps;
    if (back != _currentSteps) {
      _beginMove(back, _targetSps);
      return;
    }
  }
  _currentSps = 0;
  _targetSps = 0;
  _rotating = false;
  // Le driver reste alimente pour tenir la position si aucune pin /ENABLE n'est
  // cablee ; sinon on le coupe, ce qui evite de chauffer entre deux notes.
  _setEnabled(false);
  markDeactivation();
}

void StepperActuator::stop() {
  if (!_gpio) return;
  _currentSps = 0;
  _targetSps = 0;
  _rotating = false;
  _homing = false;
  _returnAfterMove = false;
  _targetSteps = _currentSteps;
  _gpio->digitalWrite(_config.hwPin, false);
  _setEnabled(false);
  markDeactivation();
}

void StepperActuator::service(uint32_t nowUs) {
  if (!_active || !_gpio) return;

  // Distance restante : elle borne la vitesse pour que le moteur arrive a
  // l'arret plutot qu'en pleine vitesse (sinon la charge continue sur son
  // inertie et les pas sont perdus). Une rotation continue ou une prise
  // d'origine n'a pas de cible: pas de deceleration.
  uint32_t remaining = 0xFFFFFFFFUL;
  if (!_rotating && !_homing) {
    int32_t delta = _targetSteps - _currentSteps;
    remaining = (uint32_t)(delta < 0 ? -delta : delta);
  }

  _updateSpeed(nowUs, remaining);
  if (_currentSps == 0) {
    _lastServiceUs = nowUs;
    return;
  }

  uint32_t intervalUs = 1000000UL / _currentSps;
  if (intervalUs == 0) intervalUs = 1;

  uint32_t elapsed = nowUs - _lastStepUs;
  if (elapsed < intervalUs) return;

  uint32_t due = elapsed / intervalUs;
  if (due > STEPPER_MAX_STEPS_PER_PASS) {
    // On ne rattrape pas un retard illimite : emettre 500 pas d'un coup
    // bloquerait la boucle temps reel. Le retard est absorbe en repartant de
    // maintenant, ce qui se traduit par un mouvement legerement plus lent que
    // demande plutot que par un a-coup.
    due = STEPPER_MAX_STEPS_PER_PASS;
    _lastStepUs = nowUs;
  } else {
    _lastStepUs += due * intervalUs;
  }

  for (uint32_t i = 0; i < due; i++) {
    if (_homing) {
      if (_endStopHit(_config.endStopPin1)) break;   // checkEndStops finalise
      _emitStep();
      continue;
    }

    if (_rotating) {
      _emitStep();
      continue;
    }

    if (_currentSteps == _targetSteps) {
      _finishMove();
      return;
    }
    _emitStep();
    _currentSteps += _dirForward ? 1 : -1;
  }

  if (!_homing && !_rotating && _currentSteps == _targetSteps) {
    _finishMove();
  }
}

void StepperActuator::_updateSpeed(uint32_t nowUs, uint32_t remainingSteps) {
  if (_config.stepperAccelSps2 == 0) {
    _currentSps = _targetSps;      // pas de rampe: vitesse appliquee directement
    _lastServiceUs = nowUs;
    return;
  }

  // Plafond de deceleration : v = sqrt(2 * a * d), la vitesse maximale depuis
  // laquelle il reste possible de s'arreter en `remainingSteps` pas.
  uint16_t ceiling = _targetSps;
  if (remainingSteps != 0xFFFFFFFFUL) {
    float vMax = sqrtf(2.0f * (float)_config.stepperAccelSps2 * (float)remainingSteps);
    if (vMax < (float)ceiling) ceiling = (vMax < 1.0f) ? 1 : (uint16_t)vMax;
  }

  uint32_t dtUs = nowUs - _lastServiceUs;
  _lastServiceUs = nowUs;
  if (dtUs == 0) return;
  if (dtUs > 100000UL) dtUs = 100000UL;   // borne apres une pause longue

  uint32_t deltaSps = ((uint64_t)_config.stepperAccelSps2 * dtUs) / 1000000ULL;
  if (deltaSps == 0) deltaSps = 1;

  if (_currentSps < ceiling) {
    uint32_t next = (uint32_t)_currentSps + deltaSps;
    _currentSps = next > ceiling ? ceiling : (uint16_t)next;
  } else if (_currentSps > ceiling) {
    _currentSps = (deltaSps >= _currentSps) ? ceiling
                                            : (uint16_t)(_currentSps - deltaSps);
    if (_currentSps < ceiling) _currentSps = ceiling;
  }
}

void StepperActuator::_emitStep() {
  _gpio->digitalWrite(_config.hwPin, true);
  delayMicroseconds(STEPPER_PULSE_US);
  _gpio->digitalWrite(_config.hwPin, false);
  delayMicroseconds(STEPPER_PULSE_US);
}

void StepperActuator::_setDirection(bool forward) {
  _dirForward = forward;
  _gpio->digitalWrite(_config.hwAddress, _config.inverted ? !forward : forward);
}

void StepperActuator::_setEnabled(bool on) {
  if (_config.enablePin == 0xFF) return;
  _gpio->digitalWrite(_config.enablePin, !on);   // /ENABLE actif bas
}

bool StepperActuator::_endStopHit(uint8_t pin) const {
  if (pin == 0xFF) return false;
  return ::digitalRead(pin) == LOW;   // INPUT_PULLUP, contact vers la masse
}

bool StepperActuator::checkEndStops(uint32_t nowUs) {
  if (!_active) return false;

  // Butee d'origine : elle definit le zero machine.
  if (_endStopHit(_config.endStopPin1)) {
    _currentSteps = _minSteps();
    if (_homing) {
      _homing = false;
      _homed = true;
      DBGF("[Actuator] Stepper '%s' homed at %d steps\n", _config.name, (int)_currentSteps);
      // Reprendre le deplacement qui avait declenche la prise d'origine.
      int32_t pending = _targetSteps;
      _currentSps = 0;
      if (pending != _currentSteps) {
        _beginMove(pending, _maxSps());
        return false;
      }
      _finishMove();
      return true;
    }
    if (!_dirForward && !_rotating) {
      DBGF("[Safety] Stepper '%s' hit min end stop\n", _config.name);
      _targetSteps = _currentSteps;
      _finishMove();
      return true;
    }
    if (_rotating && !_dirForward) {
      stop();
      return true;
    }
  }

  if (_endStopHit(_config.endStopPin2) && _dirForward) {
    _currentSteps = _maxSteps();
    DBGF("[Safety] Stepper '%s' hit max end stop\n", _config.name);
    _targetSteps = _currentSteps;
    if (_rotating) {
      stop();
    } else {
      _finishMove();
    }
    return true;
  }

  return false;
}

bool StepperActuator::checkTimeout(uint32_t nowUs) {
  if (!_active) return false;

  uint32_t elapsed = nowUs - _activationStartUs;

  if (_homing && elapsed >= STEPPER_HOMING_TIMEOUT_US) {
    // La butee n'a jamais repondu : cablage, contact ou sens de rotation. La
    // position reste declaree INCONNUE pour qu'un deplacement absolu ulterieur
    // retente une prise d'origine au lieu de partir d'un zero invente.
    DBGF("[Safety] Stepper '%s' homing timeout - forced stop\n", _config.name);
    _homed = false;
    stop();
    return true;
  }

  // STEPPER_ROTATE est le seul mode ou tourner longtemps est normal ; il reste
  // borne par la meme limite que les moteurs DC.
  if (elapsed >= MOTOR_MAX_CONTINUOUS_US) {
    DBGF("[Safety] Stepper '%s' timeout after %lu us - forced stop\n",
         _config.name, (unsigned long)elapsed);
    stop();
    return true;
  }
  return false;
}
