#include "actuator_manager.h"
#include "../scheduler/scheduler.h"

// Spinlock for _activeCount shared between Core 0 (web/watchdog) and Core 1 (scheduler dispatch)
static portMUX_TYPE _actMgrMux = portMUX_INITIALIZER_UNLOCKED;

ActuatorManager::ActuatorManager() : _count(0) {
  memset(_actuators, 0, sizeof(_actuators));
  memset(_idMap, 0xFF, sizeof(_idMap));
}

bool ActuatorManager::registerActuator(uint8_t id, Actuator* actuator) {
  if (!actuator || _count >= MAX_ACTUATORS) {
    DBGF("[ActMgr] Cannot register actuator id=%d (full or null)\n", id);
    return false;
  }

  // Verifier que l'ID n'existe pas deja
  if (_findIndex(id) >= 0) {
    DBGF("[ActMgr] Actuator id=%d already registered\n", id);
    return false;
  }

  _actuators[_count] = actuator;
  _idMap[_count] = id;
  _count++;

  DBGF("[ActMgr] Registered actuator '%s' (id=%d, total=%d)\n",
       actuator->getConfig().name, id, _count);
  return true;
}

bool ActuatorManager::unregisterActuator(uint8_t id) {
  int8_t idx = _findIndex(id);
  if (idx < 0) return false;

  // Shift les elements restants
  for (uint8_t i = idx; i < _count - 1; i++) {
    _actuators[i] = _actuators[i + 1];
    _idMap[i] = _idMap[i + 1];
  }
  _count--;
  _actuators[_count] = nullptr;
  _idMap[_count] = 0xFF;
  return true;
}

void ActuatorManager::dispatch(const ActuatorCommand& cmd) {
  int8_t idx = _findIndex(cmd.actuator_id);
  if (idx < 0) return;

  Actuator* act = _actuators[idx];
  if (!act || !act->isEnabled()) return;

  // Protection surcharge: check + execute + recount under spinlock
  // to prevent race between Core 0 (watchdog) and Core 1 (scheduler)
  portENTER_CRITICAL(&_actMgrMux);

  if ((CommandType)cmd.command_type != CommandType::OFF) {
    if (!act->isActive() && _activeCount >= MAX_CONCURRENT_ACTIVE) {
      portEXIT_CRITICAL(&_actMgrMux);
      DBGF("[Safety] Overload protection: %d actuators active, refusing new activation\n",
           _activeCount);
      return;
    }
  }

  act->execute(cmd);
  _updateActiveCountLocked();

  portEXIT_CRITICAL(&_actMgrMux);
}

void ActuatorManager::stopAll() {
  for (uint8_t i = 0; i < _count; i++) {
    if (_actuators[i]) {
      _actuators[i]->stop();
    }
  }
}

void ActuatorManager::clearAll() {
  stopAll();
  memset(_actuators, 0, sizeof(_actuators));
  memset(_idMap, 0xFF, sizeof(_idMap));
  _count = 0;
}

void ActuatorManager::initAll() {
  for (uint8_t i = 0; i < _count; i++) {
    if (_actuators[i]) {
      _actuators[i]->init();
    }
  }
  DBGF("[ActMgr] Initialized %d actuators\n", _count);
}

Actuator* ActuatorManager::getActuator(uint8_t id) {
  int8_t idx = _findIndex(id);
  if (idx < 0) return nullptr;
  return _actuators[idx];
}

const Actuator* ActuatorManager::getActuator(uint8_t id) const {
  int8_t idx = _findIndex(id);
  if (idx < 0) return nullptr;
  return _actuators[idx];
}

void ActuatorManager::testActuator(uint8_t id, uint8_t value) {
  Actuator* act = getActuator(id);
  if (!act) return;

  DBGF("[ActMgr] Test actuator '%s' (id=%d, val=%d)\n",
       act->getConfig().name, id, value);

  const ActuatorConfig& cfg = act->getConfig();

  switch (cfg.type) {
    case ActuatorType::SOLENOID: {
      // Utiliser le scheduler pour generer ON + OFF (securite)
      uint32_t durationUs = map(value, 0, 127,
                                (uint32_t)cfg.paramMin * 1000,
                                (uint32_t)cfg.paramMax * 1000);
      if (_scheduler) {
        _scheduler->schedulePulse(id, value, durationUs);
      } else {
        // Fallback: executer direct + OFF apres duree par defaut
        ActuatorCommand cmd;
        cmd.actuator_id = id;
        cmd.command_type = (uint8_t)CommandType::PULSE;
        cmd.value = value;
        cmd.duration = 0;
        cmd.execute_at = micros();
        act->execute(cmd);
      }
      break;
    }
    case ActuatorType::SERVO: {
      ActuatorCommand cmd;
      cmd.actuator_id = id;
      cmd.command_type = (uint8_t)CommandType::POSITION;
      cmd.value = value;
      cmd.duration = 0;
      cmd.execute_at = micros();
      act->execute(cmd);
      break;
    }
    case ActuatorType::PWM_MOTOR:
    case ActuatorType::MOTOR_OPTICAL: {
      ActuatorCommand cmd;
      cmd.actuator_id = id;
      cmd.command_type = (uint8_t)CommandType::PWM;
      cmd.value = value;
      cmd.duration = 0;
      cmd.execute_at = micros();
      act->execute(cmd);
      break;
    }
    default:
      break;
  }
}

// --- Safety ---

uint8_t ActuatorManager::checkWatchdog() {
  uint32_t now = micros();
  uint8_t stopped = 0;

  portENTER_CRITICAL(&_actMgrMux);
  for (uint8_t i = 0; i < _count; i++) {
    if (_actuators[i] && _actuators[i]->isActive()) {
      if (_actuators[i]->checkTimeout(now)) {
        stopped++;
      }
    }
  }
  if (stopped > 0) _updateActiveCountLocked();
  portEXIT_CRITICAL(&_actMgrMux);
  return stopped;
}

void ActuatorManager::updateActiveCount() {
  portENTER_CRITICAL(&_actMgrMux);
  _updateActiveCountLocked();
  portEXIT_CRITICAL(&_actMgrMux);
}

void ActuatorManager::_updateActiveCountLocked() {
  uint8_t active = 0;
  for (uint8_t i = 0; i < _count; i++) {
    if (_actuators[i] && _actuators[i]->isActive()) {
      active++;
    }
  }
  _activeCount = active;
}

int8_t ActuatorManager::_findIndex(uint8_t id) const {
  for (uint8_t i = 0; i < _count; i++) {
    if (_idMap[i] == id) return i;
  }
  return -1;
}
