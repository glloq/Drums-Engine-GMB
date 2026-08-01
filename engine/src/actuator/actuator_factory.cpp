#include "actuator_factory.h"
#include "actuator_validation.h"

// ----------------------------------------------------------------------------
// Hardware resource conflict detection (review #13)
// ----------------------------------------------------------------------------
// Collect the physical ESP32 GPIOs a config occupies (output pin on a GPIO bus,
// end stops, optical sensor pin, motor direction pin).
static void collectGpios(const ActuatorConfig& c, uint8_t* out, uint8_t& n) {
  n = 0;
  auto add = [&](uint8_t p) {
    if (p == 0xFF || p > 39 || n >= 8) return;
    for (uint8_t i = 0; i < n; i++) if (out[i] == p) return;  // dedup
    out[n++] = p;
  };
  if (c.bus == HardwareBus::GPIO_DIRECT || c.bus == HardwareBus::LEDC_PWM) add(c.hwPin);
  add(c.endStopPin1);
  add(c.endStopPin2);
  if (c.type == ActuatorType::MOTOR_OPTICAL) add(c.hwAddress);  // sensor pin (GPIO)
  if (c.type == ActuatorType::PWM_MOTOR &&
      (c.behavior == ActuatorBehavior::MOTOR_SWEEP ||
       c.behavior == ActuatorBehavior::MOTOR_ALTERNATE) &&
      c.bus == HardwareBus::LEDC_PWM &&
      c.hwAddress > 0 && c.hwAddress < 40) {
    add(c.hwAddress);  // direction pin (GPIO)
  }
}

// I2C-expander resource (bus + address + channel/pin). Returns false for
// GPIO/LEDC buses.
static bool expanderResource(const ActuatorConfig& c, uint8_t& addr, uint8_t& chan) {
  if (c.bus == HardwareBus::MCP23017 || c.bus == HardwareBus::PCA9685) {
    addr = c.hwAddress;
    chan = c.hwPin;
    return true;
  }
  return false;
}

// True if configs `a` and `b` fight over any physical resource.
static bool configsConflict(const ActuatorConfig& a, const ActuatorConfig& b) {
  uint8_t ag[8], bg[8], an, bn;
  collectGpios(a, ag, an);
  collectGpios(b, bg, bn);
  for (uint8_t i = 0; i < an; i++)
    for (uint8_t j = 0; j < bn; j++)
      if (ag[i] == bg[j]) return true;  // shared physical GPIO

  uint8_t aAddr, aChan, bAddr, bChan;
  if (expanderResource(a, aAddr, aChan) && expanderResource(b, bAddr, bChan)) {
    if (a.bus == b.bus && aAddr == bAddr && aChan == bChan) return true;
  }
  return false;
}

ActuatorFactory::ActuatorFactory(ActuatorManager* manager,
                                  MCP23017Driver* mcpDrivers, uint8_t mcpCount,
                                  PCA9685Driver* pcaDrivers, uint8_t pcaCount,
                                  GpioDriver* gpioDriver, LedcDriver* ledcDriver)
  : _manager(manager),
    _mcpDrivers(mcpDrivers), _mcpCount(mcpCount),
    _pcaDrivers(pcaDrivers), _pcaCount(pcaCount),
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
  for (uint8_t i = 0; i < _configCount; i++) {
    if (configsConflict(cfg, _configPool[i])) {
      DBGF("[Factory] Rejected actuator '%s': hardware resource conflict with id %d\n",
           cfg.name, _configPool[i].id);
      return -1;
    }
  }

  _configPool[_configCount] = cfg;
  _configCount++;
  return _configCount - 1;
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

uint8_t ActuatorFactory::rebuildAll() {
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
      if (idx < _mcpCount && _mcpDrivers[idx].isReady()) {
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
  if (idx < _pcaCount && _pcaDrivers[idx].isReady()) {
    return &_pcaDrivers[idx];
  }
  return nullptr;
}
