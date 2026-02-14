#ifndef MOTOR_ACTUATOR_H
#define MOTOR_ACTUATOR_H

#include "actuator.h"
#include "../hal/hal_interface.h"

// ============================================================================
// MotorActuator - Moteur DC via PWM
// ============================================================================

class MotorActuator : public Actuator {
public:
  MotorActuator(HalDriver* driver);

  void init() override;
  void execute(const ActuatorCommand& cmd) override;
  void stop() override;

private:
  HalDriver* _driver;
  uint16_t _currentSpeed;
};

#endif // MOTOR_ACTUATOR_H
