#include "actuator_manager.h"
#include "servo_actuator.h"
#include "../scheduler/scheduler.h"
#include "../core/panic_state.h"

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
  // P0 #3: while the emergency-stop latch is set, refuse every command except
  // OFF — this is the last-line guard that catches an ON scheduled on Core 1
  // during/after a panic triggered on Core 0.
  if (g_panicActive.load(std::memory_order_acquire) &&
      (CommandType)cmd.command_type != CommandType::OFF) {
    return;
  }

  int8_t idx = _findIndex(cmd.actuator_id);
  if (idx < 0) return;

  Actuator* act = _actuators[idx];
  if (!act || !act->isEnabled()) return;

  // H4 fix: Reduce spinlock scope — only hold lock for overload check,
  // execute outside lock (I2C writes can be slow), then re-lock to update count.
  if ((CommandType)cmd.command_type != CommandType::OFF) {
    portENTER_CRITICAL(&_actMgrMux);
    if (!act->isActive() && _activeCount >= MAX_CONCURRENT_ACTIVE) {
      portEXIT_CRITICAL(&_actMgrMux);
      DBGF("[Safety] Overload protection: %d actuators active, refusing new activation\n",
           _activeCount);
      return;
    }
    portEXIT_CRITICAL(&_actMgrMux);
  }

  act->execute(cmd);

  portENTER_CRITICAL(&_actMgrMux);
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
  if (g_panicActive.load(std::memory_order_acquire)) return;  // P0 #3
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

void ActuatorManager::testServoAngle(uint8_t id, uint8_t angle) {
  if (g_panicActive.load(std::memory_order_acquire)) return;  // P0 #3
  Actuator* act = getActuator(id);
  if (!act) return;
  if (act->getConfig().type != ActuatorType::SERVO) return;
  DBGF("[ActMgr] Test servo '%s' raw angle=%d\n", act->getConfig().name, angle);
  static_cast<ServoActuator*>(act)->moveToRawAngle(angle);
}

// --- Safety ---

uint8_t ActuatorManager::checkWatchdog() {
  uint32_t now = micros();

  // P0 #8/#10: NEVER perform slow I/O (GPIO / I2C / PWM / motor stop) while
  // holding the spinlock — it blocks interrupts and the scheduler on the other
  // core. Snapshot the currently-active actuators under the lock (cheap pointer
  // copies), release it, then run checkTimeout() (which does the actual I/O)
  // outside the critical section.
  Actuator* activeList[MAX_ACTUATORS];
  uint8_t n = 0;

  portENTER_CRITICAL(&_actMgrMux);
  for (uint8_t i = 0; i < _count; i++) {
    if (_actuators[i] && _actuators[i]->isActive()) {
      activeList[n++] = _actuators[i];
    }
  }
  portEXIT_CRITICAL(&_actMgrMux);

  uint8_t stopped = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (activeList[i]->checkTimeout(now)) {  // slow I/O, outside the lock
      stopped++;
    }
  }

  if (stopped > 0) {
    updateActiveCount();  // re-acquires the lock internally
  }
  return stopped;
}

uint8_t ActuatorManager::checkEndStops() {
  uint32_t now = micros();

  // Same snapshot-then-act pattern as checkWatchdog: never do GPIO I/O under the
  // spinlock. checkEndStops() reads limit-switch pins (blocking debounce) and
  // may stop/reverse a motor.
  Actuator* activeList[MAX_ACTUATORS];
  uint8_t n = 0;

  portENTER_CRITICAL(&_actMgrMux);
  for (uint8_t i = 0; i < _count; i++) {
    if (_actuators[i] && _actuators[i]->isActive()) {
      activeList[n++] = _actuators[i];
    }
  }
  portEXIT_CRITICAL(&_actMgrMux);

  uint8_t stopped = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (activeList[i]->checkEndStops(now)) {
      stopped++;
    }
  }

  if (stopped > 0) {
    updateActiveCount();
  }
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
