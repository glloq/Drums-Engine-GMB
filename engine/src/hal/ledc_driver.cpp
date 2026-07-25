#include "ledc_driver.h"

LedcDriver::LedcDriver() : _used(0) {
  memset(_pinForChannel, 0xFF, sizeof(_pinForChannel));
}

bool LedcDriver::begin() {
  DBGLN("[HAL] ESP32 LEDC PWM ready (GPIO-based, lazy channel allocation)");
  return true;
}

int8_t LedcDriver::_channelForPin(uint8_t pin) const {
  for (uint8_t ch = 0; ch < LEDC_CHANNELS; ch++) {
    if (_pinForChannel[ch] == pin) return (int8_t)ch;
  }
  return -1;
}

bool LedcDriver::ensureAttached(uint8_t pin) {
  if (pin > 39) return false;                  // not a valid ESP32 GPIO
  if (_channelForPin(pin) >= 0) return true;   // already attached
  if (_used >= LEDC_CHANNELS) return false;    // all channels in use

  uint8_t ch = _used;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  // Core 3.x: ledcAttach auto-manages the channel; writes are addressed by pin.
  if (!ledcAttach(pin, LEDC_FREQ_HZ, LEDC_RES_BITS)) return false;
#else
  // Core 2.x: explicit channel setup + pin attach; writes are addressed by channel.
  ledcSetup(ch, LEDC_FREQ_HZ, LEDC_RES_BITS);
  ledcAttachPin(pin, ch);
#endif
  _pinForChannel[ch] = pin;
  _used++;
  DBGF("[HAL] LEDC pin%d -> ch%d (%luHz, %dbit)\n",
       pin, ch, (unsigned long)LEDC_FREQ_HZ, (int)LEDC_RES_BITS);
  return true;
}

void LedcDriver::pwmWrite(uint8_t pin, uint16_t value) {
  if (!ensureAttached(pin)) return;
  if (value > 255) value = 255;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, value);                       // by GPIO pin
#else
  int8_t ch = _channelForPin(pin);
  if (ch < 0) return;
  ledcWrite((uint8_t)ch, value);               // by channel
#endif
}

void LedcDriver::digitalWrite(uint8_t pin, bool value) {
  // On/off via full/zero duty on the same LEDC channel.
  pwmWrite(pin, value ? 255 : 0);
}

bool LedcDriver::digitalRead(uint8_t pin) {
  (void)pin;
  return false;  // LEDC est un driver de sortie uniquement
}
