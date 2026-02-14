#ifndef SOLENOID_ACTUATOR_H
#define SOLENOID_ACTUATOR_H

#include "actuator.h"
#include "../hal/hal_interface.h"

// ============================================================================
// SolenoidActuator - Solenoide (frappe impulsion ou maintien PWM)
// ============================================================================

class SolenoidActuator : public Actuator {
public:
  SolenoidActuator(HalDriver* driver);

  void init() override;
  void execute(const ActuatorCommand& cmd) override;
  void stop() override;

private:
  HalDriver* _driver;
};

#endif // SOLENOID_ACTUATOR_H
