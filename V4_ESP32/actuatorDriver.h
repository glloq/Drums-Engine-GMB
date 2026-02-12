#ifndef ACTUATOR_DRIVER_H
#define ACTUATOR_DRIVER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <Adafruit_PWMServoDriver.h>
#include "config.h"
#include "actuatorTypes.h"
#include "scheduler.h"

// ============================================================================
// Hardware abstraction layer for all actuator types on ESP32
// ============================================================================

class ActuatorDriver {
public:
  ActuatorDriver(Scheduler* scheduler);

  // Initialize I2C and detect modules
  bool begin();

  // Activate an actuator with a value (0-127 for velocity, or angle)
  void activate(const ActuatorConfig& cfg, uint8_t value);

  // Deactivate an actuator (release/rest position)
  void deactivate(const ActuatorConfig& cfg);

  // Test an actuator with a specific value
  void test(const ActuatorConfig& cfg, uint8_t value = 80);

  // Check if a specific module is present on I2C
  bool isModulePresent(HardwareBus bus, uint8_t address);

  // Scan and report all I2C devices
  String scanI2C();

  // Get module status
  uint8_t getMcpCount() const { return _mcpCount; }
  uint8_t getPcaCount() const { return _pcaCount; }

private:
  Scheduler* _scheduler;

  // MCP23017 modules (GPIO - solenoids)
  Adafruit_MCP23X17 _mcp[MCP_MAX_MODULES];
  bool _mcpReady[MCP_MAX_MODULES];
  uint8_t _mcpCount;

  // PCA9685 modules (PWM - servos)
  Adafruit_PWMServoDriver _pca[PCA_MAX_MODULES];
  bool _pcaReady[PCA_MAX_MODULES];
  uint8_t _pcaCount;

  // ESP32 GPIO/LEDC state
  bool _ledcInitialized[16];   // 16 LEDC channels

  // Internal helpers
  uint8_t _mcpIndex(uint8_t address);
  uint8_t _pcaIndex(uint8_t address);

  void _activateSolenoidStrike(const ActuatorConfig& cfg, uint8_t velocity);
  void _activateSolenoidHold(const ActuatorConfig& cfg, uint8_t velocity);
  void _activateServoPosition(const ActuatorConfig& cfg, uint8_t value);
  void _activateServoStrike(const ActuatorConfig& cfg, uint8_t velocity);
  void _activateCombBrush(const ActuatorConfig& cfg, uint8_t velocity);
  void _activatePitchBend(const ActuatorConfig& cfg, uint8_t value);

  // Callbacks for scheduled deactivation
  struct DeactivateData {
    ActuatorDriver* driver;
    HardwareBus bus;
    uint8_t address;
    uint8_t pin;
    uint16_t restValue;      // Angle or 0 for off
  };
  static DeactivateData _deactivatePool[SCHEDULER_MAX_EVENTS];
  static uint8_t _deactivatePoolIdx;

  static void _deactivateCallback(void* data);

  // Servo pulse helpers
  uint16_t _angleToPulse(uint8_t angle);
};

#endif // ACTUATOR_DRIVER_H
