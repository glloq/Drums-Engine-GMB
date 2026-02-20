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
  SolenoidActuator() : _driver(nullptr), _holdTransitioned(false) {}
  SolenoidActuator(HalDriver* driver);

  void init() override;
  void execute(const ActuatorCommand& cmd) override;
  void stop() override;
  bool checkTimeout(uint32_t nowUs) override;

private:
  HalDriver* _driver;
  bool _holdTransitioned;  // True once HOLD PWM has dropped from activation to hold level
  uint32_t _strikeDurationUs = 0;  // Auto-stop duration for STRIKE (0 = use safety max)
};

#endif // SOLENOID_ACTUATOR_H
