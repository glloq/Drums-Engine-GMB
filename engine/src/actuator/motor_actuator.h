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
//   - End stop detection (micro-switches on endStopPin1/endStopPin2)
// Optical tracking :
//   - Uses hardware interrupt (ISR) on rising edge for edge counting
//   - ESP32 portMUX spinlock for atomic ISR/main-thread access
// ============================================================================

class MotorActuator : public Actuator {
public:
  MotorActuator() : _driver(nullptr), _currentSpeed(0), _sensorPin(0),
                    _targetEdges(0), _positionTracking(false),
                    _homing(false), _endStopReached(false) {}
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
  bool _homing;                      // True during homing sequence (move to end stop)
  bool _endStopReached;              // Last end stop state

  // --- ISR infrastructure (per-instance via pin table) ---
  static constexpr uint8_t MAX_OPTICAL_MOTORS = 4;
  struct IsrSlot {
    volatile uint32_t edgeCount;
    MotorActuator* instance;
    portMUX_TYPE mux;
  };
  static IsrSlot _isrSlots[MAX_OPTICAL_MOTORS];
  static uint8_t _isrSlotPins[MAX_OPTICAL_MOTORS]; // GPIO pin for each slot (0xFF = free)

  int8_t _isrSlotIdx = -1;                  // Slot index for this instance (-1 = none)

  static void IRAM_ATTR _opticalISR0();
  static void IRAM_ATTR _opticalISR1();
  static void IRAM_ATTR _opticalISR2();
  static void IRAM_ATTR _opticalISR3();
  static constexpr void (*_isrTable[MAX_OPTICAL_MOTORS])() = {
    _opticalISR0, _opticalISR1, _opticalISR2, _opticalISR3
  };

  void _attachOpticalISR();                  // Arm the interrupt
  void _detachOpticalISR();                  // Disarm the interrupt
  void _updateOpticalTracking(uint32_t nowUs);
  bool _checkEndStops();                     // Returns true if any end stop triggered
};

#endif // MOTOR_ACTUATOR_H
