#ifndef SERVO_ACTUATOR_H
#define SERVO_ACTUATOR_H

#include "actuator.h"
#include "../hal/pca9685_driver.h"

// ============================================================================
// ServoActuator - Servo (position, frappe oscillante, peigne/brosse, pitch)
// ============================================================================
// Securite :
//   - Angle clampe entre paramMin et paramMax
//   - Inversion supportee
// ============================================================================

class ServoActuator : public Actuator {
public:
  ServoActuator() : _driver(nullptr), _currentAngle(0) {}
  ServoActuator(PCA9685Driver* driver);

  void init() override;
  void execute(const ActuatorCommand& cmd) override;
  void stop() override;

  // Position actuelle du servo
  uint8_t getCurrentAngle() const { return _currentAngle; }

private:
  PCA9685Driver* _driver;
  uint8_t _currentAngle;

  void _moveToAngle(uint8_t angle);
};

#endif // SERVO_ACTUATOR_H
