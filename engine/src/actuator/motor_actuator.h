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
// Optical tracking :
//   - Uses hardware interrupt (ISR) on rising edge for edge counting
//   - ESP32 portMUX spinlock for atomic ISR/main-thread access
// ============================================================================

class MotorActuator : public Actuator {
public:
  MotorActuator() : _driver(nullptr), _currentSpeed(0), _sensorPin(0),
                    _targetEdges(0), _positionTracking(false) {}
  MotorActuator(HalDriver* driver);

  void init() override;
  void execute(const ActuatorCommand& cmd) override;
  void stop() override;
  bool checkTimeout(uint32_t nowUs) override;

private:
  HalDriver* _driver;
  uint16_t _currentSpeed;
  uint8_t _sensorPin;               // GPIO pin for optical sensor (from _config.hwAddress)
  uint32_t _targetEdges;
  bool _positionTracking;

  // --- ISR infrastructure ---
  static volatile uint32_t _isrEdgeCount;   // Edge counter incremented in ISR
  static MotorActuator* _isrInstance;        // Active instance receiving ISR events
  static portMUX_TYPE _isrMux;              // Spinlock for atomic access

  static void IRAM_ATTR _opticalISR();       // Hardware interrupt callback

  void _attachOpticalISR();                  // Arm the interrupt
  void _detachOpticalISR();                  // Disarm the interrupt
  void _updateOpticalTracking(uint32_t nowUs);
};

#endif // MOTOR_ACTUATOR_H
