#include "pca9685_driver.h"

PCA9685Driver::PCA9685Driver(uint8_t address)
  : _pca(address, Wire), _address(address), _ready(false) {}

bool PCA9685Driver::begin() {
  if (!_pca.begin()) {
    DBGF("[HAL] PCA9685 @ 0x%02X: init failed\n", _address);
    _ready = false;
    return false;
  }

  _pca.setOscillatorFrequency(27000000);
  _pca.setPWMFreq(PCA_FREQUENCY);

  _ready = true;
  DBGF("[HAL] PCA9685 @ 0x%02X: OK (16 PWM, %dHz)\n", _address, PCA_FREQUENCY);
  return true;
}

void PCA9685Driver::digitalWrite(uint8_t pin, bool value) {
  if (!_ready || pin >= 16) return;
  _pca.setPWM(pin, 0, value ? 4095 : 0);
}

void PCA9685Driver::pwmWrite(uint8_t channel, uint16_t value) {
  if (!_ready || channel >= 16) return;
  if (value > 4095) value = 4095;
  _pca.setPWM(channel, 0, value);
}

bool PCA9685Driver::digitalRead(uint8_t pin) {
  // PCA9685 est un driver de sortie uniquement
  return false;
}

uint16_t PCA9685Driver::angleToPulse(uint8_t angle) const {
  // Map 0-180 deg vers pulse PCA9685 (150-600 pour servos standard a 50Hz)
  return map(angle, 0, 180, 150, 600);
}

void PCA9685Driver::setServoPulse(uint8_t channel, uint16_t pulse) {
  if (!_ready || channel >= 16) return;
  _pca.setPWM(channel, 0, pulse);
}
