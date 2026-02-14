#ifndef SOLENOID_ACTUATOR_H
#define SOLENOID_ACTUATOR_H

#include "actuator.h"
#include "../hal/hal_interface.h"

// ============================================================================
// SolenoidActuator - Solenoide (frappe impulsion ou maintien PWM)
// ============================================================================
// Securite :
//   - Cooldown entre frappes (protection thermique)
//   - Timeout max ON (SOLENOID_MAX_ON_US) pour eviter surchauffe
// ============================================================================

class SolenoidActuator : public Actuator {
public:
  SolenoidActuator() : _driver(nullptr) {}
  SolenoidActuator(HalDriver* driver);

  void init() override;
  void execute(const ActuatorCommand& cmd) override;
  void stop() override;
  bool checkTimeout(uint32_t nowUs) override;

private:
  HalDriver* _driver;
};

#endif // SOLENOID_ACTUATOR_H
