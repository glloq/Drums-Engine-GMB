#include "mcp23017_driver.h"

MCP23017Driver::MCP23017Driver(uint8_t address)
  : _address(address), _ready(false) {}

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
  DBGF("[HAL] MCP23017 @ 0x%02X: OK (16 GPIO)\n", _address);
  return true;
}

void MCP23017Driver::digitalWrite(uint8_t pin, bool value) {
  if (!_ready || pin >= 16) return;
  _mcp.digitalWrite(pin, value ? HIGH : LOW);
}

void MCP23017Driver::pwmWrite(uint8_t channel, uint16_t value) {
  // MCP23017 ne supporte pas le PWM, on fait du tout-ou-rien
  if (!_ready || channel >= 16) return;
  _mcp.digitalWrite(channel, value > 0 ? HIGH : LOW);
}

bool MCP23017Driver::digitalRead(uint8_t pin) {
  if (!_ready || pin >= 16) return false;
  return _mcp.digitalRead(pin) == HIGH;
}
