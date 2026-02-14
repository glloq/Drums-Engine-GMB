#ifndef GPIO_DRIVER_H
#define GPIO_DRIVER_H

#include "hal_interface.h"
#include "../core/config.h"

// ============================================================================
// ESP32 GPIO Direct Driver
// ============================================================================

class GpioDriver : public HalDriver {
public:
  GpioDriver();

  bool begin() override;
  void digitalWrite(uint8_t pin, bool value) override;
  void pwmWrite(uint8_t channel, uint16_t value) override;
  bool digitalRead(uint8_t pin) override;
  bool isReady() const override { return true; }
  uint8_t channelCount() const override { return 40; }

  // Configurer une pin en sortie
  void configureOutput(uint8_t pin);
};

#endif // GPIO_DRIVER_H
