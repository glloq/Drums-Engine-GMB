#include "mcp23017_driver.h"
#include "hal_interface.h"

MCP23017Driver::MCP23017Driver(uint8_t address)
  : _address(address), _ready(false), _errorCount(0) {}

bool MCP23017Driver::begin() {
  if (!_mcp.begin_I2C(_address, &Wire)) {
    DBGF("[HAL] MCP23017 @ 0x%02X: init failed\n", _address);
    _ready = false;
    return false;
  }

  // Configure toutes les pins en sortie, etat LOW
  for (uint8_t p = 0; p < 16; p++) {
    _mcp.pinMode(p, OUTPUT);
    _mcp.digitalWrite(p, LOW);
  }

  _ready = true;
  _errorCount = 0;
  DBGF("[HAL] MCP23017 @ 0x%02X: OK (16 GPIO)\n", _address);
  return true;
}

void MCP23017Driver::digitalWrite(uint8_t pin, bool value) {
  if (!_ready || pin >= 16) return;
  if (!i2cLock()) return;
  _mcp.digitalWrite(pin, value ? HIGH : LOW);
  if (_checkI2CError()) { i2cUnlock(); return; }
  i2cUnlock();
}

void MCP23017Driver::pwmWrite(uint8_t channel, uint16_t value) {
  // MCP23017 ne supporte pas le PWM, on fait du tout-ou-rien
  if (!_ready || channel >= 16) return;
  if (!i2cLock()) return;
  _mcp.digitalWrite(channel, value > 0 ? HIGH : LOW);
  if (_checkI2CError()) { i2cUnlock(); return; }
  i2cUnlock();
}

bool MCP23017Driver::digitalRead(uint8_t pin) {
  if (!_ready || pin >= 16) return false;
  if (!i2cLock()) return false;
  bool val = _mcp.digitalRead(pin) == HIGH;
  _checkI2CError();
  i2cUnlock();
  return val;
}

bool MCP23017Driver::_checkI2CError() {
  // Check I2C bus error via Wire.lastError (ESP32 specific)
  if (Wire.lastError() != 0) {
    _errorCount++;
    if (_errorCount >= 10) {
      DBGF("[HAL] MCP23017 @ 0x%02X: too many I2C errors (%d), attempting recovery\n",
           _address, _errorCount);
      Wire.flush();
      _errorCount = 0;
      // Try to re-init
      if (!_mcp.begin_I2C(_address, &Wire)) {
        DBGF("[HAL] MCP23017 @ 0x%02X: recovery failed, marking offline\n", _address);
        _ready = false;
      } else {
        DBGF("[HAL] MCP23017 @ 0x%02X: recovery OK\n", _address);
      }
    }
    return true;
  }
  return false;
}
