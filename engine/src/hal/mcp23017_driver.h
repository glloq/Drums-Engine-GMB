#ifndef MCP23017_DRIVER_H
#define MCP23017_DRIVER_H

#include <Adafruit_MCP23X17.h>
#include "hal_interface.h"
#include "../core/config.h"

// ============================================================================
// MCP23017 Driver - 16-channel I2C GPIO expander
// ============================================================================
// Utilise pour les solenoide on/off
// Adresses I2C: 0x20-0x27
// ============================================================================

class MCP23017Driver : public HalDriver {
public:
  MCP23017Driver(uint8_t address = MCP_BASE_ADDRESS);

  bool begin() override;
  void digitalWrite(uint8_t pin, bool value) override;
  void pwmWrite(uint8_t channel, uint16_t value) override;
  bool digitalRead(uint8_t pin) override;
  bool isReady() const override { return _ready; }
  uint8_t channelCount() const override { return 16; }

  uint8_t getAddress() const { return _address; }

private:
  Adafruit_MCP23X17 _mcp;
  uint8_t _address;
  bool _ready;
};

#endif // MCP23017_DRIVER_H
