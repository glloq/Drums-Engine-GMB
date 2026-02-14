#include "actuator_manager.h"

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
  if (act && act->isEnabled()) {
    act->execute(cmd);
  }
}

void ActuatorManager::stopAll() {
  for (uint8_t i = 0; i < _count; i++) {
    if (_actuators[i]) {
      _actuators[i]->stop();
    }
  }
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

  ActuatorCommand cmd;
  cmd.actuator_id = id;
  cmd.value = value;
  cmd.duration = 0;
  cmd.execute_at = micros();

  // Choisir le type de commande selon le type d'actuateur
  switch (act->getConfig().type) {
    case ActuatorType::SOLENOID:
      cmd.command_type = (uint8_t)CommandType::PULSE;
      break;
    case ActuatorType::SERVO:
      cmd.command_type = (uint8_t)CommandType::POSITION;
      break;
    case ActuatorType::PWM_MOTOR:
      cmd.command_type = (uint8_t)CommandType::PWM;
      break;
    default:
      cmd.command_type = (uint8_t)CommandType::PULSE;
      break;
  }

  act->execute(cmd);
}

int8_t ActuatorManager::_findIndex(uint8_t id) const {
  for (uint8_t i = 0; i < _count; i++) {
    if (_idMap[i] == id) return i;
  }
  return -1;
}
