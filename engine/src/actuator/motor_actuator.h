#ifndef MOTOR_ACTUATOR_H
#define MOTOR_ACTUATOR_H

#include "actuator.h"
#include "../hal/hal_interface.h"

// ============================================================================
// MotorActuator - Moteur DC via PWM
// ============================================================================
// Securite :
//   - Limite duty-cycle continu (MOTOR_MAX_CONTINUOUS_US)
//   - Arret force apres timeout
// ============================================================================

class MotorActuator : public Actuator {
public:
  MotorActuator() : _driver(nullptr), _currentSpeed(0), _lastSensorState(false), _edgeCount(0), _targetEdges(0), _positionTracking(false) {}
  MotorActuator(HalDriver* driver);

  void init() override;
  void execute(const ActuatorCommand& cmd) override;
  void stop() override;
  bool checkTimeout(uint32_t nowUs) override;

private:
  HalDriver* _driver;
  uint16_t _currentSpeed;
  bool _lastSensorState;
  uint32_t _edgeCount;
  uint32_t _targetEdges;
  bool _positionTracking;

  void _updateOpticalTracking(uint32_t nowUs);
};

#endif // MOTOR_ACTUATOR_H
