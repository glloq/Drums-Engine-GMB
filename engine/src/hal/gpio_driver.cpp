#include "gpio_driver.h"

GpioDriver::GpioDriver() {}

bool GpioDriver::begin() {
  DBGLN("[HAL] ESP32 GPIO: ready");
  return true;
}

void GpioDriver::configureOutput(uint8_t pin) {
  pinMode(pin, OUTPUT);
  ::digitalWrite(pin, LOW);
}

void GpioDriver::digitalWrite(uint8_t pin, bool value) {
  ::digitalWrite(pin, value ? HIGH : LOW);
}

void GpioDriver::pwmWrite(uint8_t channel, uint16_t value) {
  // GPIO ne supporte pas PWM, tout-ou-rien
  ::digitalWrite(channel, value > 0 ? HIGH : LOW);
}

bool GpioDriver::digitalRead(uint8_t pin) {
  return ::digitalRead(pin) == HIGH;
}
