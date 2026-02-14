#include "scheduler.h"

Scheduler::Scheduler(ActuatorManager* actuatorMgr)
  : _actuatorMgr(actuatorMgr), _dispatchCount(0),
    _lastJitterUs(0), _maxJitterUs(0) {}

void Scheduler::begin() {
  _queue.clear();
  _dispatchCount = 0;
  resetJitterStats();
  DBGLN("[Scheduler] Started (queue lock-free, timestamps us)");
}

bool Scheduler::scheduleCommand(const ActuatorCommand& cmd) {
  return _queue.push(cmd);
}

bool Scheduler::schedulePulse(uint8_t actuatorId, uint16_t value, uint32_t durationUs) {
  uint32_t now = micros();
  return schedulePulseAt(actuatorId, value, durationUs, now);
}

bool Scheduler::schedulePulseAt(uint8_t actuatorId, uint16_t value,
                                 uint32_t durationUs, uint32_t executeAt) {
  // Commande ON
  ActuatorCommand cmdOn;
  cmdOn.actuator_id = actuatorId;
  cmdOn.command_type = (uint8_t)CommandType::PULSE;
  cmdOn.value = value;
  cmdOn.duration = (uint16_t)(durationUs / 100); // Stocke en centaines de us
  cmdOn.execute_at = executeAt;

  if (!_queue.push(cmdOn)) return false;

  // Commande OFF programmee apres la duree
  ActuatorCommand cmdOff;
  cmdOff.actuator_id = actuatorId;
  cmdOff.command_type = (uint8_t)CommandType::OFF;
  cmdOff.value = 0;
  cmdOff.duration = 0;
  cmdOff.execute_at = executeAt + durationUs;

  return _queue.push(cmdOff);
}

void Scheduler::update() {
  uint32_t now = micros();

  // Traiter toutes les commandes dont le timestamp est passe
  while (_queue.hasDue(now)) {
    ActuatorCommand cmd;
    if (!_queue.pop(cmd)) break;

    // Mesurer le jitter
    int32_t jitter = (int32_t)(now - cmd.execute_at);
    if (jitter < 0) jitter = -jitter;
    _lastJitterUs = (uint32_t)jitter;
    if (_lastJitterUs > _maxJitterUs) {
      _maxJitterUs = _lastJitterUs;
    }

    // Dispatcher vers l'actuateur
    _actuatorMgr->dispatch(cmd);
    _dispatchCount++;
  }
}

void Scheduler::cancelAll() {
  _queue.clear();
  DBGLN("[Scheduler] All commands cancelled");
}

void Scheduler::resetJitterStats() {
  _lastJitterUs = 0;
  _maxJitterUs = 0;
}
