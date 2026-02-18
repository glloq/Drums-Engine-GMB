#ifndef PCA9685_DRIVER_H
#define PCA9685_DRIVER_H

#include <Adafruit_PWMServoDriver.h>
#include "hal_interface.h"
#include "../core/config.h"

// ============================================================================
// PCA9685 Driver - 16-channel I2C PWM controller
// ============================================================================
// Utilise pour les servos et solenoide PWM
// Adresses I2C: 0x40-0x7F
// Frequence servo: 50Hz
// Resolution PWM: 12 bits (0-4095)
// ============================================================================

class PCA9685Driver : public HalDriver {
public:
  PCA9685Driver(uint8_t address = PCA_BASE_ADDRESS);

  bool begin() override;
  void digitalWrite(uint8_t pin, bool value) override;
  void pwmWrite(uint8_t channel, uint16_t value) override;
  bool digitalRead(uint8_t pin) override;
  bool isReady() const override { return _ready; }
  uint8_t channelCount() const override { return 16; }

  uint8_t getAddress() const { return _address; }

  // Servo-specific: convertir angle en pulse PCA9685
  uint16_t angleToPulse(uint8_t angle) const;

  // Ecrire directement un pulse servo
  void setServoPulse(uint8_t channel, uint16_t pulse);

private:
  Adafruit_PWMServoDriver _pca;
  uint8_t _address;
  bool _ready;
  uint8_t _errorCount;

  bool _checkI2CError();
};

#endif // PCA9685_DRIVER_H
