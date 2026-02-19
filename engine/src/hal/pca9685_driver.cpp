#include "pca9685_driver.h"
#include "hal_interface.h"

PCA9685Driver::PCA9685Driver(uint8_t address)
  : _pca(address, Wire), _address(address), _ready(false), _errorCount(0) {}

bool PCA9685Driver::begin() {
  if (!_pca.begin()) {
    DBGF("[HAL] PCA9685 @ 0x%02X: init failed\n", _address);
    _ready = false;
    return false;
  }

  _pca.setOscillatorFrequency(27000000);
  _pca.setPWMFreq(PCA_FREQUENCY);

  _ready = true;
  _errorCount = 0;
  DBGF("[HAL] PCA9685 @ 0x%02X: OK (16 PWM, %dHz)\n", _address, PCA_FREQUENCY);
  return true;
}

void PCA9685Driver::digitalWrite(uint8_t pin, bool value) {
  if (!_ready || pin >= 16) return;
  if (!i2cLock()) return;
  _pca.setPWM(pin, 0, value ? 4095 : 0);
  _checkI2CError();
  i2cUnlock();
}

void PCA9685Driver::pwmWrite(uint8_t channel, uint16_t value) {
  if (!_ready || channel >= 16) return;
  if (value > 4095) value = 4095;
  if (!i2cLock()) return;
  _pca.setPWM(channel, 0, value);
  _checkI2CError();
  i2cUnlock();
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
  if (!i2cLock()) return;
  _pca.setPWM(channel, 0, pulse);
  _checkI2CError();
  i2cUnlock();
}

bool PCA9685Driver::_checkI2CError() {
  // Probe I2C device to detect bus errors (portable across ESP32 Core 2.x/3.x)
  Wire.beginTransmission(_address);
  uint8_t err = Wire.endTransmission();
  if (err != 0) {
    _errorCount++;
    if (_errorCount >= 10) {
      DBGF("[HAL] PCA9685 @ 0x%02X: too many I2C errors (%d), attempting recovery\n",
           _address, _errorCount);
      Wire.flush();
      _errorCount = 0;
      if (!_pca.begin()) {
        DBGF("[HAL] PCA9685 @ 0x%02X: recovery failed, marking offline\n", _address);
        _ready = false;
      } else {
        _pca.setOscillatorFrequency(27000000);
        _pca.setPWMFreq(PCA_FREQUENCY);
        DBGF("[HAL] PCA9685 @ 0x%02X: recovery OK\n", _address);
      }
    }
    return true;
  }
  return false;
}
