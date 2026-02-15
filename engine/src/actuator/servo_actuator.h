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
  bool checkTimeout(uint32_t nowUs) override;

  // Position actuelle du servo
  uint8_t getCurrentAngle() const { return _currentAngle; }

private:
  PCA9685Driver* _driver;
  uint8_t _currentAngle;
  bool _combBrushRunning = false;
  uint32_t _lastCombBrushStepUs = 0;
  bool _combBrushHigh = false;
  uint32_t _combBrushPeriodUs = 120000;

  // Hi-hat controller state
  uint8_t _lastCcValue = 0;
  bool _splashActive = false;
  uint32_t _splashStartUs = 0;
  uint8_t _splashReturnAngle = 0;
  static constexpr uint32_t SPLASH_DURATION_US = 80000; // 80ms splash open

  void _moveToAngle(uint8_t angle);
};

#endif // SERVO_ACTUATOR_H
