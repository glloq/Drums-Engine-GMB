#ifndef MOTOR_ACTUATOR_H
#define MOTOR_ACTUATOR_H

#include "actuator.h"
#include "../hal/hal_interface.h"

// ============================================================================
// MotorActuator - Moteur DC via PWM
// ============================================================================
// Comportements :
//   MOTOR_SPEED         : Vitesse continue (velocity → PWM)
//   MOTOR_TIMED         : Tourne pendant une duree, puis arret auto
//   MOTOR_SWEEP         : 1 end stop → arret, 2 end stops → aller-retour complet
//   MOTOR_ALTERNATE     : Alterne direction entre end stops (hwAddress=dirPin)
//   MOTOR_OPTICAL_TRACK : Suivi optique par comptage ISR
//
// Commandes acceptees :
//   PWM      → vitesse directe (tous behaviors)
//   PULSE    → declenchement (TIMED/SWEEP/ALTERNATE)
//   POSITION → suivi optique (OPTICAL_TRACK uniquement)
//   OFF      → arret immediat
//
// Securite :
//   - MOTOR_MAX_CONTINUOUS_US timeout sur tous les behaviors
//   - End stop detection (micro-switches sur endStopPin1/endStopPin2)
//   - Arret force par watchdog
// ============================================================================

class MotorActuator : public Actuator {
public:
  MotorActuator() : _driver(nullptr), _currentSpeed(0),
                    _sensorPin(0xFF), _dirPin(0xFF), _forward(true),
                    _targetEdges(0), _positionTracking(false),
                    _timedDurationUs(0), _alternateRunning(false),
                    _sweepPhase(0),
                    _homing(false), _endStopReached(false) {}
  MotorActuator(HalDriver* driver);

  void init() override;
  void execute(const ActuatorCommand& cmd) override;
  void stop() override;
  bool checkTimeout(uint32_t nowUs) override;
  bool checkEndStops(uint32_t nowUs) override;   // review #5: 1 kHz polling

private:
  HalDriver* _driver;
  uint16_t _currentSpeed;

  // --- Pin secondaire (selon behavior) ---
  uint8_t _sensorPin;               // OPTICAL: GPIO capteur (from hwAddress)
  uint8_t _dirPin;                  // ALTERNATE/SWEEP: GPIO direction (from hwAddress)
  bool _forward;                    // Direction courante (true = avant)

  // --- Suivi optique ---
  uint32_t _targetEdges;
  bool _positionTracking;

  // --- Etats modes moteur ---
  uint32_t _timedDurationUs;        // Duree pour MOTOR_TIMED (us)
  bool _alternateRunning;           // MOTOR_ALTERNATE en cours
  uint8_t _sweepPhase;              // MOTOR_SWEEP: 0=aller, 1=retour (2 end stops)

  bool _homing;
  bool _endStopReached;

  // --- Direction (H-bridge DIR pin) ---
  void _setDirection(bool forward);

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
