#include "scheduler.h"
#include <math.h>

Scheduler::Scheduler(ActuatorManager* actuatorMgr)
  : _actuatorMgr(actuatorMgr), _dispatchCount(0),
    _lastJitterUs(0), _maxJitterUs(0) {}

void Scheduler::begin() {
  _queue.clear();
  _dispatchCount = 0;
  resetJitterStats();
  DBGLN("[Scheduler] Started (time-sorted queue, spinlock-protected, timestamps us)");
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
  uint32_t durationUnits = durationUs / 100;
  cmdOn.duration = (durationUnits > UINT16_MAX) ? UINT16_MAX : (uint16_t)durationUnits;
  cmdOn.execute_at = executeAt;

  // Commande OFF programmee apres la duree
  ActuatorCommand cmdOff;
  cmdOff.actuator_id = actuatorId;
  cmdOff.command_type = (uint8_t)CommandType::OFF;
  cmdOff.value = 0;
  cmdOff.duration = 0;
  cmdOff.execute_at = executeAt + durationUs;

  // P0 #7: insert ON+OFF transactionally so an actuator is never turned ON
  // without its scheduled OFF (which would leave it energized until the
  // watchdog trips).
  return _queue.pushPair(cmdOn, cmdOff);
}


bool Scheduler::scheduleActionSteps(const ActionStep* steps, uint8_t stepCount,
                                    uint8_t velocity, uint32_t timestamp,
                                    const uint16_t* globalVars,
                                    uint8_t activeGroup) {
  if (!steps || stepCount == 0) return true;

  auto applyCurve = [](uint8_t inputValue, uint8_t curve) -> uint8_t {
    float normalized = inputValue / 127.0f;
    switch (curve) {
      case CURVE_EXPO:
        return (uint8_t)(normalized * normalized * 127.0f);
      case CURVE_LOG:
        return (uint8_t)(sqrtf(normalized) * 127.0f);
      case CURVE_LINEAR:
      default:
        return inputValue;
    }
  };

  bool allScheduled = true;

  for (uint8_t i = 0; i < stepCount; i++) {
    const ActionStep& step = steps[i];
    if (step.actuator_id == 0xFF) continue;

    // Alternation filter: skip steps not in the active group
    if (activeGroup > 0 && step.alternate_group > 0 && step.alternate_group != activeGroup) continue;

    uint8_t curvedVelocity = applyCurve(velocity, step.velocity_curve);

    uint16_t value = 0;
    switch ((ValueSource)step.value_source) {
      case ValueSource::VELOCITY:
        value = curvedVelocity;
        break;
      case ValueSource::FIXED:
        value = step.value_fixed;
        break;
      case ValueSource::CC_VAR:
        value = (globalVars && step.cc_index < MAX_GLOBAL_VARS) ? globalVars[step.cc_index] : 0;
        if (value > 127) value = 127;
        break;
      case ValueSource::INVERTED_VELOCITY:
        value = 127 - curvedVelocity;
        break;
      default:
        value = curvedVelocity;
        break;
    }

    uint32_t delayUs = (uint32_t)step.delay_ms * 1000;
    uint32_t executeAt = timestamp + delayUs;
    if (executeAt < timestamp) executeAt = UINT32_MAX;  // Saturate on overflow
    CommandType cmdType = (CommandType)step.command_type;

    if (cmdType == CommandType::PULSE) {
      uint16_t minMs = step.duration_min_ms;
      uint16_t maxMs = step.duration_max_ms;

      // Fallback: si duree = 0/0, utiliser les parametres de l'actionneur
      if (minMs == 0 && maxMs == 0) {
        Actuator* act = _actuatorMgr->getActuator(step.actuator_id);
        if (act) {
          const ActuatorConfig& cfg = act->getConfig();
          if (cfg.behavior == ActuatorBehavior::SOLENOID_HOLD) {
            // C2 fix: HOLD stays on until NoteOff — don't auto-generate OFF.
            // Just schedule the ON command; solenoid_actuator's checkTimeout
            // handles the safety max via cooldownUs.
            ActuatorCommand cmd;
            cmd.actuator_id = step.actuator_id;
            cmd.command_type = (uint8_t)CommandType::PULSE;
            cmd.value = value;
            cmd.duration = 0;
            cmd.execute_at = executeAt;
            if (!scheduleCommand(cmd)) allScheduled = false;
            continue;
          } else {
            // STRIKE: paramMin/Max sont les durees min/max en ms
            minMs = cfg.paramMin;
            maxMs = cfg.paramMax;
          }
        }
      }

      if (maxMs < minMs) {
        uint16_t t = minMs;
        minMs = maxMs;
        maxMs = t;
      }

      uint32_t durationUs = (maxMs > minMs)
        ? map(curvedVelocity, 0, 127, (uint32_t)minMs * 1000, (uint32_t)maxMs * 1000)
        : (uint32_t)maxMs * 1000;

      if (!schedulePulseAt(step.actuator_id, value, durationUs, executeAt)) {
        allScheduled = false;
      }
      continue;
    }

    ActuatorCommand cmd;
    cmd.actuator_id = step.actuator_id;
    cmd.command_type = (uint8_t)cmdType;
    cmd.value = value;
    cmd.duration = 0;
    cmd.execute_at = executeAt;

    if (!scheduleCommand(cmd)) {
      allScheduled = false;
    }
  }

  return allScheduled;
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
