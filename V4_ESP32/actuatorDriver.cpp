#include "actuatorDriver.h"

// Static pool for deactivation callbacks
ActuatorDriver::DeactivateData ActuatorDriver::_deactivatePool[SCHEDULER_MAX_EVENTS];
uint8_t ActuatorDriver::_deactivatePoolIdx = 0;

ActuatorDriver::ActuatorDriver(Scheduler* scheduler)
  : _scheduler(scheduler), _mcpCount(0), _pcaCount(0) {
  for (int i = 0; i < MCP_MAX_MODULES; i++) _mcpReady[i] = false;
  for (int i = 0; i < PCA_MAX_MODULES; i++) _pcaReady[i] = false;
  for (int i = 0; i < 16; i++) _ledcInitialized[i] = false;
}

bool ActuatorDriver::begin() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_FREQ);

  DBGLN("[ActuatorDriver] Scanning I2C bus...");

  // Initialize MCP23017 modules
  _mcpCount = 0;
  for (uint8_t i = 0; i < MCP_MAX_MODULES; i++) {
    uint8_t addr = MCP_BASE_ADDRESS + i;
    if (_mcp[i].begin_I2C(addr, &Wire)) {
      _mcpReady[i] = true;
      _mcpCount++;
      // Set all pins as output
      for (uint8_t p = 0; p < 16; p++) {
        _mcp[i].pinMode(p, OUTPUT);
        _mcp[i].digitalWrite(p, LOW);
      }
      DBGF("[ActuatorDriver] MCP23017 @ 0x%02X OK (%d pins)\n", addr, 16);
    }
  }

  // Initialize PCA9685 modules
  _pcaCount = 0;
  for (uint8_t i = 0; i < PCA_MAX_MODULES; i++) {
    uint8_t addr = PCA_BASE_ADDRESS + i;
    _pca[i] = Adafruit_PWMServoDriver(addr, Wire);
    if (_pca[i].begin()) {
      _pcaReady[i] = true;
      _pcaCount++;
      _pca[i].setOscillatorFrequency(27000000);
      _pca[i].setPWMFreq(PCA_FREQUENCY);
      DBGF("[ActuatorDriver] PCA9685  @ 0x%02X OK (16 channels)\n", addr);
    }
  }

  DBGF("[ActuatorDriver] Found %d MCP23017 + %d PCA9685\n", _mcpCount, _pcaCount);
  return (_mcpCount > 0 || _pcaCount > 0);
}

void ActuatorDriver::activate(const ActuatorConfig& cfg, uint8_t value) {
  if (!cfg.enabled) return;

  switch (cfg.type) {
    case ACT_SOLENOID_STRIKE: _activateSolenoidStrike(cfg, value); break;
    case ACT_SOLENOID_HOLD:   _activateSolenoidHold(cfg, value); break;
    case ACT_SERVO_POSITION:  _activateServoPosition(cfg, value); break;
    case ACT_SERVO_STRIKE:    _activateServoStrike(cfg, value); break;
    case ACT_COMB_BRUSH:      _activateCombBrush(cfg, value); break;
    case ACT_PITCH_BEND:      _activatePitchBend(cfg, value); break;
    default: break;
  }
}

void ActuatorDriver::deactivate(const ActuatorConfig& cfg) {
  if (!cfg.enabled) return;

  switch (cfg.bus) {
    case BUS_MCP23017: {
      uint8_t idx = _mcpIndex(cfg.hwAddress);
      if (idx < MCP_MAX_MODULES && _mcpReady[idx]) {
        _mcp[idx].digitalWrite(cfg.hwPin, LOW);
      }
      break;
    }
    case BUS_PCA9685: {
      uint8_t idx = _pcaIndex(cfg.hwAddress);
      if (idx < PCA_MAX_MODULES && _pcaReady[idx]) {
        // Move to default/rest position
        if (cfg.type == ACT_SERVO_POSITION || cfg.type == ACT_SERVO_STRIKE ||
            cfg.type == ACT_COMB_BRUSH || cfg.type == ACT_PITCH_BEND) {
          _pca[idx].setPWM(cfg.hwPin, 0, _angleToPulse(cfg.paramDefault));
        } else {
          _pca[idx].setPWM(cfg.hwPin, 0, 0); // Off for solenoids
        }
      }
      break;
    }
    case BUS_GPIO_DIRECT:
      digitalWrite(cfg.hwPin, LOW);
      break;
    case BUS_LEDC_PWM:
      ledcWrite(cfg.hwPin, 0);
      break;
  }
}

void ActuatorDriver::test(const ActuatorConfig& cfg, uint8_t value) {
  DBGF("[ActuatorDriver] TEST: %s (type=%s, bus=%s, addr=0x%02X, pin=%d, val=%d)\n",
       cfg.name, actuatorTypeName(cfg.type), hardwareBusName(cfg.bus),
       cfg.hwAddress, cfg.hwPin, value);
  activate(cfg, value);
}

bool ActuatorDriver::isModulePresent(HardwareBus bus, uint8_t address) {
  switch (bus) {
    case BUS_MCP23017: {
      uint8_t idx = _mcpIndex(address);
      return (idx < MCP_MAX_MODULES && _mcpReady[idx]);
    }
    case BUS_PCA9685: {
      uint8_t idx = _pcaIndex(address);
      return (idx < PCA_MAX_MODULES && _pcaReady[idx]);
    }
    default: return true; // GPIO direct always available
  }
}

String ActuatorDriver::scanI2C() {
  String result = "";
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      char buf[32];
      snprintf(buf, sizeof(buf), "0x%02X", addr);
      if (result.length() > 0) result += ", ";
      result += buf;

      // Identify known modules
      if (addr >= 0x20 && addr <= 0x27) {
        result += " (MCP23017)";
      } else if (addr >= 0x40 && addr <= 0x7F) {
        result += " (PCA9685)";
      }
    }
  }
  return result.length() > 0 ? result : "No devices found";
}

// --- Private helpers ---

uint8_t ActuatorDriver::_mcpIndex(uint8_t address) {
  if (address >= MCP_BASE_ADDRESS && address < MCP_BASE_ADDRESS + MCP_MAX_MODULES) {
    return address - MCP_BASE_ADDRESS;
  }
  return 0xFF;
}

uint8_t ActuatorDriver::_pcaIndex(uint8_t address) {
  if (address >= PCA_BASE_ADDRESS && address < PCA_BASE_ADDRESS + PCA_MAX_MODULES) {
    return address - PCA_BASE_ADDRESS;
  }
  return 0xFF;
}

uint16_t ActuatorDriver::_angleToPulse(uint8_t angle) {
  // Map 0-180° to PCA9685 pulse (150-600 for standard servos at 50Hz)
  return map(angle, 0, 180, 150, 600);
}

void ActuatorDriver::_deactivateCallback(void* data) {
  DeactivateData* d = (DeactivateData*)data;
  if (!d || !d->driver) return;

  switch (d->bus) {
    case BUS_MCP23017: {
      uint8_t idx = d->driver->_mcpIndex(d->address);
      if (idx < MCP_MAX_MODULES && d->driver->_mcpReady[idx]) {
        d->driver->_mcp[idx].digitalWrite(d->pin, LOW);
      }
      break;
    }
    case BUS_PCA9685: {
      uint8_t idx = d->driver->_pcaIndex(d->address);
      if (idx < PCA_MAX_MODULES && d->driver->_pcaReady[idx]) {
        if (d->restValue > 0) {
          d->driver->_pca[idx].setPWM(d->pin, 0, d->driver->_angleToPulse(d->restValue));
        } else {
          d->driver->_pca[idx].setPWM(d->pin, 0, 0);
        }
      }
      break;
    }
    case BUS_GPIO_DIRECT:
      digitalWrite(d->pin, LOW);
      break;
    case BUS_LEDC_PWM:
      ledcWrite(d->pin, 0);
      break;
  }
}

// Schedule deactivation after a delay
static void scheduleDeactivate(ActuatorDriver* driver, Scheduler* sched,
                                HardwareBus bus, uint8_t addr, uint8_t pin,
                                uint16_t restValue, unsigned long delayMs) {
  uint8_t poolIdx = ActuatorDriver::_deactivatePoolIdx;
  ActuatorDriver::_deactivatePool[poolIdx] = {driver, bus, addr, pin, restValue};
  ActuatorDriver::_deactivatePoolIdx = (poolIdx + 1) % SCHEDULER_MAX_EVENTS;
  sched->scheduleOnce(delayMs, ActuatorDriver::_deactivateCallback,
                      &ActuatorDriver::_deactivatePool[poolIdx]);
}

void ActuatorDriver::_activateSolenoidStrike(const ActuatorConfig& cfg, uint8_t velocity) {
  uint16_t duration = map(velocity, 0, 127, cfg.paramMin, cfg.paramMax);

  switch (cfg.bus) {
    case BUS_MCP23017: {
      uint8_t idx = _mcpIndex(cfg.hwAddress);
      if (idx < MCP_MAX_MODULES && _mcpReady[idx]) {
        _mcp[idx].digitalWrite(cfg.hwPin, HIGH);
        scheduleDeactivate(this, _scheduler, cfg.bus, cfg.hwAddress, cfg.hwPin, 0, duration);
      }
      break;
    }
    case BUS_GPIO_DIRECT:
      pinMode(cfg.hwPin, OUTPUT);
      digitalWrite(cfg.hwPin, HIGH);
      scheduleDeactivate(this, _scheduler, cfg.bus, 0, cfg.hwPin, 0, duration);
      break;
    default:
      DBGF("[ActuatorDriver] Solenoid strike unsupported on bus %s\n", hardwareBusName(cfg.bus));
      break;
  }
}

void ActuatorDriver::_activateSolenoidHold(const ActuatorConfig& cfg, uint8_t velocity) {
  uint8_t idx = _pcaIndex(cfg.hwAddress);
  if (cfg.bus != BUS_PCA9685 || idx >= PCA_MAX_MODULES || !_pcaReady[idx]) return;

  // Full power punch, then reduce to hold level
  uint16_t punchPWM = 4095;  // 100% duty
  uint16_t holdPWM = map(velocity, 0, 127, 1024, 2048); // 25-50% duty

  _pca[idx].setPWM(cfg.hwPin, 0, punchPWM);

  // Schedule reduction to hold after punch duration
  scheduleDeactivate(this, _scheduler, cfg.bus, cfg.hwAddress, cfg.hwPin,
                     0, cfg.paramMin); // paramMin = punch duration
  // Note: hold state is managed by the caller via deactivate()
}

void ActuatorDriver::_activateServoPosition(const ActuatorConfig& cfg, uint8_t value) {
  uint8_t idx = _pcaIndex(cfg.hwAddress);
  if (cfg.bus != BUS_PCA9685 || idx >= PCA_MAX_MODULES || !_pcaReady[idx]) return;

  // Map value (0-127) to angle range
  uint8_t angle = map(value, 0, 127, cfg.paramMin, cfg.paramMax);
  _pca[idx].setPWM(cfg.hwPin, 0, _angleToPulse(angle));
}

void ActuatorDriver::_activateServoStrike(const ActuatorConfig& cfg, uint8_t velocity) {
  uint8_t idx = _pcaIndex(cfg.hwAddress);
  if (cfg.bus != BUS_PCA9685 || idx >= PCA_MAX_MODULES || !_pcaReady[idx]) return;

  // Strike angle based on velocity
  uint8_t strikeAngle = map(velocity, 0, 127, cfg.paramMin, cfg.paramMax);
  _pca[idx].setPWM(cfg.hwPin, 0, _angleToPulse(strikeAngle));

  // Return to rest after strike
  scheduleDeactivate(this, _scheduler, cfg.bus, cfg.hwAddress, cfg.hwPin,
                     cfg.paramDefault, SERVO_MOVE_TIME_MS);
}

void ActuatorDriver::_activateCombBrush(const ActuatorConfig& cfg, uint8_t velocity) {
  // Comb/brush: servo sweeps back and forth across the surface
  // paramMin = start angle, paramMax = end angle
  // velocity controls sweep amplitude
  uint8_t idx = _pcaIndex(cfg.hwAddress);
  if (cfg.bus != BUS_PCA9685 || idx >= PCA_MAX_MODULES || !_pcaReady[idx]) return;

  uint8_t amplitude = map(velocity, 0, 127, 5, (cfg.paramMax - cfg.paramMin));
  uint8_t center = (cfg.paramMin + cfg.paramMax) / 2;
  uint8_t sweepAngle = center + amplitude / 2;

  // Move to sweep position
  _pca[idx].setPWM(cfg.hwPin, 0, _angleToPulse(sweepAngle));

  // Return to center after sweep
  scheduleDeactivate(this, _scheduler, cfg.bus, cfg.hwAddress, cfg.hwPin,
                     center, SERVO_MOVE_TIME_MS * 2);
}

void ActuatorDriver::_activatePitchBend(const ActuatorConfig& cfg, uint8_t value) {
  // Pitch bend: servo/linear actuator tensions the drum skin
  // value maps directly to tension position
  uint8_t idx = _pcaIndex(cfg.hwAddress);
  if (cfg.bus != BUS_PCA9685 || idx >= PCA_MAX_MODULES || !_pcaReady[idx]) return;

  uint8_t angle = map(value, 0, 127, cfg.paramMin, cfg.paramMax);
  _pca[idx].setPWM(cfg.hwPin, 0, _angleToPulse(angle));
}
