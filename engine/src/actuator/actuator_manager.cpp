#include "actuator_manager.h"
#include "servo_actuator.h"
#include "../scheduler/scheduler.h"
#include "../core/panic_state.h"

// Spinlock for _activeCount / _activeCurrentMa shared between Core 0
// (web/watchdog) and Core 1 (scheduler dispatch)
static portMUX_TYPE _actMgrMux = portMUX_INITIALIZER_UNLOCKED;

ActuatorManager::ActuatorManager() : _count(0) {
  memset(_actuators, 0, sizeof(_actuators));
  memset(_idMap, 0xFF, sizeof(_idMap));
  memset(_servicable, 0, sizeof(_servicable));
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
  if (actuator->needsService() && _servicableCount < MAX_ACTUATORS) {
    _servicable[_servicableCount++] = actuator;
  }

  DBGF("[ActMgr] Registered actuator '%s' (id=%d, total=%d)\n",
       actuator->getConfig().name, id, _count);
  return true;
}

bool ActuatorManager::unregisterActuator(uint8_t id) {
  int8_t idx = _findIndex(id);
  if (idx < 0) return false;

  Actuator* removed = _actuators[idx];

  // Shift les elements restants
  for (uint8_t i = idx; i < _count - 1; i++) {
    _actuators[i] = _actuators[i + 1];
    _idMap[i] = _idMap[i + 1];
  }
  _count--;
  _actuators[_count] = nullptr;
  _idMap[_count] = 0xFF;

  // Retirer aussi de la liste de service, sinon la boucle temps reel
  // continuerait a faire avancer un actionneur qui n'existe plus.
  for (uint8_t i = 0; i < _servicableCount; i++) {
    if (_servicable[i] != removed) continue;
    for (uint8_t j = i; j + 1 < _servicableCount; j++) {
      _servicable[j] = _servicable[j + 1];
    }
    _servicableCount--;
    _servicable[_servicableCount] = nullptr;
    break;
  }
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
    bool admitted = act->isActive() || _admitLocked(act);
    portEXIT_CRITICAL(&_actMgrMux);
    if (!admitted) return;
  }

  act->execute(cmd);

  portENTER_CRITICAL(&_actMgrMux);
  _updateActiveCountLocked();
  portEXIT_CRITICAL(&_actMgrMux);
}

// Admission d'une NOUVELLE activation. Appele sous _actMgrMux.
// La regle elle-meme est dans core/power_budget.h (pure, testee nativement) ;
// ici on ne fait que lui fournir l'etat et comptabiliser le motif de refus.
// Aucune trace serie sous le spinlock : DBGF() ecrit sur le port serie, ce qui
// n'a rien a faire dans une section critique.
bool ActuatorManager::_admitLocked(const Actuator* act) {
  const ActuatorConfig& cfg = act->getConfig();

  switch (powerBudgetAdmit(_budget, _activeCount, _activeCurrentMa,
                           cfg.currentMa, cfg.priority)) {
    case PowerVerdict::ADMIT:              return true;
    case PowerVerdict::REFUSED_COUNT:      _refusedByCount++; return false;
    case PowerVerdict::REFUSED_PEAK:       _refusedByPeak++; return false;
    case PowerVerdict::REFUSED_CONTINUOUS: _refusedByContinuous++; return false;
  }
  return false;
}

void ActuatorManager::setPowerBudget(const PowerBudgetConfig& budget) {
  portENTER_CRITICAL(&_actMgrMux);
  _budget = budget;
  portEXIT_CRITICAL(&_actMgrMux);
  DBGF("[ActMgr] Power budget: peak=%u mA, continuous=%u mA, maxConcurrent=%u\n",
       budget.maxPeakMa, budget.maxContinuousMa, budget.maxConcurrent);
}

uint32_t ActuatorManager::getWorstCaseCurrentMa() const {
  uint32_t total = 0;
  for (uint8_t i = 0; i < _count; i++) {
    if (_actuators[i] && _actuators[i]->isEnabled()) {
      total += _actuators[i]->getConfig().currentMa;
    }
  }
  return total;
}

void ActuatorManager::resetRefusedStats() {
  portENTER_CRITICAL(&_actMgrMux);
  _refusedByCount = 0;
  _refusedByPeak = 0;
  _refusedByContinuous = 0;
  portEXIT_CRITICAL(&_actMgrMux);
}

void ActuatorManager::serviceAll(uint32_t nowUs) {
  // Snapshot sous le spinlock (pointeurs seulement), service en dehors :
  // service() fait des E/S GPIO, meme regle que checkWatchdog/checkEndStops.
  Actuator* list[MAX_ACTUATORS];
  uint8_t n;

  portENTER_CRITICAL(&_actMgrMux);
  n = _servicableCount;
  for (uint8_t i = 0; i < n; i++) list[i] = _servicable[i];
  portEXIT_CRITICAL(&_actMgrMux);

  if (n == 0) return;

  bool anyStopped = false;
  for (uint8_t i = 0; i < n; i++) {
    if (!list[i]) continue;
    bool wasActive = list[i]->isActive();
    list[i]->service(nowUs);
    if (wasActive && !list[i]->isActive()) anyStopped = true;
  }
  // Un mouvement qui s'acheve libere du budget: le rendre visible tout de suite
  // plutot qu'a la prochaine passe du watchdog (10 Hz).
  if (anyStopped) updateActiveCount();
}

void ActuatorManager::stopAll() {
  for (uint8_t i = 0; i < _count; i++) {
    if (_actuators[i]) {
      _actuators[i]->stop();
    }
  }
}

void ActuatorManager::clearAll() {
  stopAll();  // hardware I/O — keep OUTSIDE the spinlock
  // review #11: mutate the registry AND reset _activeCount under the same
  // spinlock the scheduler/watchdog use. A stale _activeCount after a rebuild
  // could otherwise falsely trip the concurrent-activation overload guard.
  portENTER_CRITICAL(&_actMgrMux);
  memset(_actuators, 0, sizeof(_actuators));
  memset(_idMap, 0xFF, sizeof(_idMap));
  memset(_servicable, 0, sizeof(_servicable));
  _servicableCount = 0;
  _count = 0;
  _activeCount = 0;
  _activeCurrentMa = 0;
  portEXIT_CRITICAL(&_actMgrMux);
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

  // Every actuation below goes through dispatch() (or the scheduler), never
  // straight to Actuator::execute(). dispatch() is where the concurrent-
  // activation limit (MAX_CONCURRENT_ACTIVE) and the cached active count live,
  // so calling execute() directly let the UI's test buttons drive more
  // actuators at once than the MIDI path would ever allow.
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
        dispatch(cmd);
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
      dispatch(cmd);
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
      dispatch(cmd);
      break;
    }
    case ActuatorType::STEPPER: {
      // Chaque comportement de stepper repond a une commande differente ;
      // envoyer la mauvaise ne ferait simplement rien et le bouton "Test" de
      // l'UI semblerait mort.
      ActuatorCommand cmd;
      cmd.actuator_id = id;
      cmd.value = value;
      cmd.duration = 0;
      cmd.execute_at = micros();
      switch (cfg.behavior) {
        case ActuatorBehavior::STEPPER_STRIKE:
          cmd.command_type = (uint8_t)CommandType::PULSE;
          break;
        case ActuatorBehavior::STEPPER_ROTATE:
          cmd.command_type = (uint8_t)CommandType::PWM;
          break;
        default:
          cmd.command_type = (uint8_t)CommandType::POSITION;
          break;
      }
      dispatch(cmd);
      break;
    }
    default:
      break;
  }
}

// Deliberate exception to the "everything through dispatch()" rule: raw-angle
// calibration bypasses paramMin/paramMax on purpose, which no ActuatorCommand
// can express. It stays direct, but only for a servo — a low-power, position-
// holding device, not something the overload limit is protecting against.
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
  uint32_t currentMa = 0;
  for (uint8_t i = 0; i < _count; i++) {
    if (_actuators[i] && _actuators[i]->isActive()) {
      active++;
      currentMa += _actuators[i]->getConfig().currentMa;
    }
  }
  _activeCount = active;
  _activeCurrentMa = currentMa;
}

int8_t ActuatorManager::_findIndex(uint8_t id) const {
  for (uint8_t i = 0; i < _count; i++) {
    if (_idMap[i] == id) return i;
  }
  return -1;
}
