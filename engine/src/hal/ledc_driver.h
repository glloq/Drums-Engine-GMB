#ifndef LEDC_DRIVER_H
#define LEDC_DRIVER_H

#include "hal_interface.h"
#include "../core/config.h"

// ============================================================================
// ESP32 LEDC PWM Driver — GPIO-based with lazy channel allocation
// ============================================================================
// Actuators address this driver by GPIO pin (config.hwPin). The driver
// allocates a LEDC channel on first use and keeps a pin->channel map, so
// callers never deal with channel numbers.
//
// Compatible with both Arduino-ESP32 2.x (ledcSetup + ledcAttachPin,
// ledcWrite by channel) and 3.x (ledcAttach, ledcWrite by pin) — selected at
// compile time via ESP_ARDUINO_VERSION_MAJOR. This fixes the build break under
// core 2.x and the previously non-initialized / channel-vs-GPIO confusion.
// ============================================================================

class LedcDriver : public HalDriver {
public:
  LedcDriver();

  bool begin() override;
  void digitalWrite(uint8_t pin, bool value) override;
  void pwmWrite(uint8_t pin, uint16_t duty) override;    // pin = GPIO, duty 0..PWM_DUTY_MAX
  bool supportsPwm() const override { return true; }     // vrai PWM materiel
  bool digitalRead(uint8_t pin) override;
  bool isReady() const override { return true; }
  uint8_t channelCount() const override { return LEDC_CHANNELS; }

  // Attach a GPIO to a free LEDC channel (idempotent). Returns false if the pin
  // is invalid or no channel is free. Called lazily by pwmWrite/digitalWrite,
  // but may also be called eagerly at setup time.
  bool ensureAttached(uint8_t pin);

private:
  static constexpr uint8_t  LEDC_CHANNELS = 16;
  static constexpr uint32_t LEDC_FREQ_HZ  = 5000;
  static constexpr uint8_t  LEDC_RES_BITS = 8;

  uint8_t _pinForChannel[LEDC_CHANNELS];  // channel -> GPIO (0xFF = free)
  uint8_t _used;                          // number of channels allocated

  int8_t _channelForPin(uint8_t pin) const;
};

#endif // LEDC_DRIVER_H
