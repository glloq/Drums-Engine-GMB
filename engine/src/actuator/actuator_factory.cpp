#include "actuator_factory.h"
#include "actuator_validation.h"
#include "../core/reconfig_barrier.h"

ActuatorFactory::ActuatorFactory(ActuatorManager* manager,
                                  MCP23017Driver* mcpDrivers, uint8_t mcpSlots,
                                  PCA9685Driver* pcaDrivers, uint8_t pcaSlots,
                                  GpioDriver* gpioDriver, LedcDriver* ledcDriver)
  : _manager(manager),
    _mcpDrivers(mcpDrivers), _mcpSlots(mcpSlots),
    _pcaDrivers(pcaDrivers), _pcaSlots(pcaSlots),
    _gpioDriver(gpioDriver), _ledcDriver(ledcDriver),
    _solenoidIdx(0), _servoIdx(0), _motorIdx(0),
    _configCount(0), _createdCount(0) {
  // Initialiser les pools servo avec un pointeur PCA par defaut
  // (sera reassigne dans createAndRegister)
}

Actuator* ActuatorFactory::createAndRegister(const ActuatorConfig& config) {
  if (_createdCount >= MAX_ACTUATORS) {
    DBGLN("[Factory] Max actuators reached!");
    return nullptr;
  }

  Actuator* actuator = nullptr;

  switch (config.type) {
    case ActuatorType::SOLENOID: {
      if (_solenoidIdx >= MAX_ACTUATORS) return nullptr;
      HalDriver* driver = _getDriver(config.bus, config.hwAddress);
      if (!driver) {
        DBGF("[Factory] No HAL driver for solenoid '%s' (bus=%d, addr=0x%02X)\n",
             config.name, (int)config.bus, config.hwAddress);
        return nullptr;
      }
      SolenoidActuator& sol = _solenoidPool[_solenoidIdx];
      sol = SolenoidActuator(driver);
      sol.setConfig(config);
      actuator = &sol;
      _solenoidIdx++;
      break;
    }

    case ActuatorType::SERVO: {
      if (_servoIdx >= MAX_ACTUATORS) return nullptr;
      PCA9685Driver* pcaDriver = _getPcaDriver(config.hwAddress);
      if (!pcaDriver) {
        DBGF("[Factory] No PCA9685 driver for servo '%s' (addr=0x%02X)\n",
             config.name, config.hwAddress);
        return nullptr;
      }
      ServoActuator& srv = _servoPool[_servoIdx];
      srv = ServoActuator(pcaDriver);
      srv.setConfig(config);
      actuator = &srv;
      _servoIdx++;
      break;
    }

    case ActuatorType::PWM_MOTOR:
    case ActuatorType::MOTOR_OPTICAL: {
      if (_motorIdx >= MAX_ACTUATORS) return nullptr;
      HalDriver* driver = _getDriver(config.bus, config.hwAddress);
      if (!driver) {
        DBGF("[Factory] No HAL driver for motor '%s' (bus=%d)\n",
             config.name, (int)config.bus);
        return nullptr;
      }
      MotorActuator& mot = _motorPool[_motorIdx];
      mot = MotorActuator(driver);
      mot.setConfig(config);
      // Attach the LEDC channel eagerly here (build time, Core 0) so the RT
      // dispatch path never triggers a first-use allocation — closes the narrow
      // two-core first-attach race in LedcDriver.
      if (config.bus == HardwareBus::LEDC_PWM && _ledcDriver) {
        _ledcDriver->ensureAttached(config.hwPin);
      }
      actuator = &mot;
      _motorIdx++;
      break;
    }

    default:
      DBGF("[Factory] Unknown actuator type %d\n", (int)config.type);
      return nullptr;
  }

  if (actuator) {
    if (_manager->registerActuator(config.id, actuator)) {
      actuator->init();
      _createdCount++;
      DBGF("[Factory] Created '%s' (id=%d, type=%s, bus=%s, pin=%d)\n",
           config.name, config.id,
           actuatorTypeName(config.type),
           hardwareBusName(config.bus),
           config.hwPin);
      return actuator;
    } else {
      DBGF("[Factory] Failed to register '%s' (id=%d)\n", config.name, config.id);
    }
  }

  return nullptr;
}

uint8_t ActuatorFactory::createAll(const ActuatorConfig* configs, uint8_t count) {
  uint8_t created = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (configs[i].enabled) {
      if (createAndRegister(configs[i])) {
        created++;
      }
    }
  }
  DBGF("[Factory] Created %d/%d actuators\n", created, count);
  return created;
}

int ActuatorFactory::addConfig(const ActuatorConfig& config) {
  if (_configCount >= MAX_ACTUATORS) return -1;

  // P1 #15: reject invalid configs at the common funnel (LittleFS load, V4
  // migration, templates and the API all pass through here) so a corrupted or
  // hand-edited file cannot inject an out-of-range GPIO or a bad type/bus pair.
  const char* err = nullptr;
  if (!validateActuatorConfig(config, err)) {
    DBGF("[Factory] Rejected invalid actuator config '%s': %s\n",
         config.name, err ? err : "?");
    return -1;
  }

  // Assigner un ID unique si pas deja defini
  ActuatorConfig cfg = config;
  if (cfg.id == 0) {
    uint8_t maxId = 0;
    for (uint8_t i = 0; i < _configCount; i++) {
      if (_configPool[i].id > maxId) maxId = _configPool[i].id;
    }
    cfg.id = maxId + 1;
  }

  // review #9: 0xFF is the reserved "invalid/none" sentinel, and duplicate ids
  // would make the second config silently fail to register at rebuild time.
  if (cfg.id == 0xFF) {
    DBGLN("[Factory] Rejected actuator config: id 255 is reserved");
    return -1;
  }
  for (uint8_t i = 0; i < _configCount; i++) {
    if (_configPool[i].id == cfg.id) {
      DBGF("[Factory] Rejected actuator config: duplicate id %d\n", cfg.id);
      return -1;
    }
  }

  // review #13: reject a config that would share a physical pin/channel with an
  // already-registered one (two outputs on the same GPIO, MCP pin or PCA channel).
  uint8_t conflictId = 0;
  if (hasResourceConflict(cfg, 0xFF, conflictId)) {
    DBGF("[Factory] Rejected actuator '%s': hardware resource conflict with id %d\n",
         cfg.name, conflictId);
    return -1;
  }

  _configPool[_configCount] = cfg;
  _configCount++;
  return _configCount - 1;
}

bool ActuatorFactory::hasResourceConflict(const ActuatorConfig& candidate,
                                          uint8_t excludeId,
                                          uint8_t& conflictId) const {
  for (uint8_t i = 0; i < _configCount; i++) {
    if (_configPool[i].id == excludeId) continue;  // the config being edited
    if (actuatorConfigsConflict(candidate, _configPool[i])) {
      conflictId = _configPool[i].id;
      return true;
    }
  }
  return false;
}

bool ActuatorFactory::removeConfig(uint8_t id) {
  for (uint8_t i = 0; i < _configCount; i++) {
    if (_configPool[i].id == id) {
      for (uint8_t j = i; j < _configCount - 1; j++) {
        _configPool[j] = _configPool[j + 1];
      }
      _configCount--;
      return true;
    }
  }
  return false;
}

ActuatorConfig* ActuatorFactory::findConfig(uint8_t id) {
  for (uint8_t i = 0; i < _configCount; i++) {
    if (_configPool[i].id == id) return &_configPool[i];
  }
  return nullptr;
}

int ActuatorFactory::rebuildAll() {
  // clearAll() vide le registre et createAll() reconstruit les objets dans les
  // pools statiques — deux operations que le Scheduler lit en continu depuis
  // Core 1. On met donc le coeur temps reel en pause pendant la reconstruction
  // (voir core/reconfig_barrier.h) ; sans cela, un dispatch concurrent pouvait
  // atteindre un Actuator en cours de reaffectation.
  //
  // FAIL-CLOSED : si le RT ne confirme pas son arret, on n'ecrit RIEN. Ceder
  // ici (l'ancien comportement : un simple avertissement puis reconstruction)
  // revenait a desactiver la barriere exactement dans le cas ou elle est
  // necessaire — le RT est occupe, donc probablement en train de lire.
  ReconfigLock lock;
  if (!lock.ok()) {
    DBGLN("[Factory] Rebuild refused: RT core did not park");
    return REBUILD_BUSY;
  }

  // Nettoyer les enregistrements existants dans le manager
  _manager->clearAll();

  // Reset les pools
  _solenoidIdx = 0;
  _servoIdx = 0;
  _motorIdx = 0;
  _createdCount = 0;

  return createAll(_configPool, _configCount);
}

HalDriver* ActuatorFactory::_getDriver(HardwareBus bus, uint8_t hwAddress) {
  switch (bus) {
    case HardwareBus::MCP23017: {
      if (!_mcpDrivers) return nullptr;
      if (hwAddress < MCP_BASE_ADDRESS) return nullptr;  // C4 fix: prevent underflow
      uint8_t idx = hwAddress - MCP_BASE_ADDRESS;
      // Bound by the array size, then ask the slot itself whether the module
      // answered at boot. Bounding by "number of modules found" instead would
      // hide every module behind a gap in the address range (0x20 absent +
      // 0x21 present => count 1 => index 1 rejected).
      if (idx < _mcpSlots && _mcpDrivers[idx].isReady()) {
        return &_mcpDrivers[idx];
      }
      return nullptr;
    }

    case HardwareBus::PCA9685: {
      PCA9685Driver* pca = _getPcaDriver(hwAddress);
      return pca; // PCA9685Driver herite de HalDriver
    }

    case HardwareBus::GPIO_DIRECT:
      return _gpioDriver;

    case HardwareBus::LEDC_PWM:
      return _ledcDriver;

    default:
      return nullptr;
  }
}

PCA9685Driver* ActuatorFactory::_getPcaDriver(uint8_t hwAddress) {
  if (!_pcaDrivers) return nullptr;
  if (hwAddress < PCA_BASE_ADDRESS) return nullptr;  // C4 fix: prevent underflow
  uint8_t idx = hwAddress - PCA_BASE_ADDRESS;
  // Bounded by array size, not by module count — see _getDriver() above.
  if (idx < _pcaSlots && _pcaDrivers[idx].isReady()) {
    return &_pcaDrivers[idx];
  }
  return nullptr;
}
