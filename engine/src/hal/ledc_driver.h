#ifndef LEDC_DRIVER_H
#define LEDC_DRIVER_H

#include "hal_interface.h"
#include "../core/config.h"

// ============================================================================
// ESP32 LEDC PWM Driver - 16 channels PWM natif
// ============================================================================

class LedcDriver : public HalDriver {
public:
  LedcDriver();

  bool begin() override;
  void digitalWrite(uint8_t pin, bool value) override;
  void pwmWrite(uint8_t channel, uint16_t value) override;
  bool digitalRead(uint8_t pin) override;
  bool isReady() const override { return true; }
  uint8_t channelCount() const override { return 16; }

  // Configurer un channel LEDC
  void configureChannel(uint8_t channel, uint8_t pin, uint32_t freq = 5000, uint8_t resolution = 8);

private:
  bool _initialized[16];
};

#endif // LEDC_DRIVER_H
