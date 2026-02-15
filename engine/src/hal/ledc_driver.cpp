#include "ledc_driver.h"

LedcDriver::LedcDriver() {
  memset(_initialized, 0, sizeof(_initialized));
  memset(_channelPin, 0xFF, sizeof(_channelPin));
}

bool LedcDriver::begin() {
  DBGLN("[HAL] ESP32 LEDC PWM: ready (16 channels)");
  return true;
}

void LedcDriver::configureChannel(uint8_t channel, uint8_t pin, uint32_t freq, uint8_t resolution) {
  if (channel >= 16) return;
  // ESP32 Arduino Core 3.x: ledcAttach replaces ledcSetup + ledcAttachPin
  ledcAttach(pin, freq, resolution);
  _channelPin[channel] = pin;
  _initialized[channel] = true;
  DBGF("[HAL] LEDC ch%d -> pin%d (%dHz, %dbit)\n", channel, pin, freq, resolution);
}

void LedcDriver::digitalWrite(uint8_t pin, bool value) {
  // Find channel for this pin
  for (uint8_t ch = 0; ch < 16; ch++) {
    if (_channelPin[ch] == pin && _initialized[ch]) {
      ledcWrite(pin, value ? 255 : 0);
      return;
    }
  }
}

void LedcDriver::pwmWrite(uint8_t channel, uint16_t value) {
  if (channel >= 16 || !_initialized[channel]) return;
  if (value > 255) value = 255;
  // ESP32 Arduino Core 3.x: ledcWrite uses pin, not channel
  ledcWrite(_channelPin[channel], value);
}

bool LedcDriver::digitalRead(uint8_t pin) {
  return false; // LEDC est un driver de sortie uniquement
}
