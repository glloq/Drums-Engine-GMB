#include "ledc_driver.h"

LedcDriver::LedcDriver() {
  memset(_initialized, 0, sizeof(_initialized));
}

bool LedcDriver::begin() {
  DBGLN("[HAL] ESP32 LEDC PWM: ready (16 channels)");
  return true;
}

void LedcDriver::configureChannel(uint8_t channel, uint8_t pin, uint32_t freq, uint8_t resolution) {
  if (channel >= 16) return;
  ledcSetup(channel, freq, resolution);
  ledcAttachPin(pin, channel);
  _initialized[channel] = true;
  DBGF("[HAL] LEDC ch%d -> pin%d (%dHz, %dbit)\n", channel, pin, freq, resolution);
}

void LedcDriver::digitalWrite(uint8_t pin, bool value) {
  // Utiliser le channel comme index
  if (pin < 16 && _initialized[pin]) {
    ledcWrite(pin, value ? 255 : 0);
  }
}

void LedcDriver::pwmWrite(uint8_t channel, uint16_t value) {
  if (channel >= 16 || !_initialized[channel]) return;
  if (value > 255) value = 255;
  ledcWrite(channel, value);
}

bool LedcDriver::digitalRead(uint8_t pin) {
  return false; // LEDC est un driver de sortie uniquement
}
