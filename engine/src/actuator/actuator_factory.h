#ifndef ACTUATOR_FACTORY_H
#define ACTUATOR_FACTORY_H

#include "../core/types.h"
#include "../core/config.h"
#include "actuator.h"
#include "actuator_manager.h"
#include "solenoid_actuator.h"
#include "servo_actuator.h"
#include "motor_actuator.h"
#include "../hal/mcp23017_driver.h"
#include "../hal/pca9685_driver.h"
#include "../hal/gpio_driver.h"
#include "../hal/ledc_driver.h"

// ============================================================================
// ActuatorFactory - Creation et enregistrement des actionneurs
// ============================================================================
// Cree les instances Actuator a partir des ActuatorConfig,
// les lie aux bons drivers HAL, et les enregistre dans l'ActuatorManager.
//
// Pool statique pre-alloue (zero allocation dynamique en runtime).
// ============================================================================

class ActuatorFactory {
public:
  // `mcpSlots`/`pcaSlots` are the SIZES of the driver arrays (MCP_MAX_MODULES /
  // PCA_MAX_MODULES), not the number of modules that answered the I2C scan.
  // Drivers are indexed by (address - base), so a gap in the populated
  // addresses (e.g. 0x20 absent, 0x21 present) must not shrink the addressable
  // range — readiness is decided per slot by MCP23017Driver::isReady().
  ActuatorFactory(ActuatorManager* manager,
                  MCP23017Driver* mcpDrivers, uint8_t mcpSlots,
                  PCA9685Driver* pcaDrivers, uint8_t pcaSlots,
                  GpioDriver* gpioDriver, LedcDriver* ledcDriver);

  // Creer un actionneur a partir de sa config et l'enregistrer
  // Retourne le pointeur vers l'actuateur cree, ou nullptr si erreur
  Actuator* createAndRegister(const ActuatorConfig& config);

  // Creer tous les actionneurs depuis un tableau de configs
  uint8_t createAll(const ActuatorConfig* configs, uint8_t count);

  // Nombre d'actionneurs crees
  uint8_t getCreatedCount() const { return _createdCount; }

  // Acces au pool de configs (pour serialisation)
  const ActuatorConfig& getConfig(uint8_t index) const { return _configPool[index]; }
  ActuatorConfig& getConfig(uint8_t index) { return _configPool[index]; }
  uint8_t getConfigCount() const { return _configCount; }

  // Ajouter une config au pool (depuis JSON, etc.)
  int addConfig(const ActuatorConfig& config);
  bool removeConfig(uint8_t id);
  ActuatorConfig* findConfig(uint8_t id);

  // review #13 (follow-up): shared hardware-resource conflict check.
  // Returns true if `candidate` would fight over a physical GPIO, MCP23017 pin
  // or PCA9685 channel already claimed by another config. `excludeId` is the id
  // to ignore (0xFF for none) — an UPDATE must not conflict with itself.
  // On conflict, `conflictId` receives the id of the offending config.
  //
  // Both the create path (addConfig) and the REST update path go through this,
  // so editing an actuator can no longer move it onto a pin that is already in
  // use — a check that previously existed only at creation time.
  bool hasResourceConflict(const ActuatorConfig& candidate, uint8_t excludeId,
                           uint8_t& conflictId) const;

  // Reconstruire tous les actionneurs depuis le pool de configs
  uint8_t rebuildAll();

private:
  ActuatorManager* _manager;

  // HAL drivers references. The counts are ARRAY SIZES (addressable slots), not
  // the number of modules present on the bus — see the constructor comment.
  MCP23017Driver* _mcpDrivers;
  uint8_t _mcpSlots;
  PCA9685Driver* _pcaDrivers;
  uint8_t _pcaSlots;
  GpioDriver* _gpioDriver;
  LedcDriver* _ledcDriver;

  // Pool statique d'actionneurs (pas de new/delete)
  SolenoidActuator _solenoidPool[MAX_ACTUATORS];
  ServoActuator _servoPool[MAX_ACTUATORS];
  MotorActuator _motorPool[MAX_ACTUATORS];
  uint8_t _solenoidIdx;
  uint8_t _servoIdx;
  uint8_t _motorIdx;

  // Pool de configs
  ActuatorConfig _configPool[MAX_ACTUATORS];
  uint8_t _configCount;

  uint8_t _createdCount;

  // Trouver le bon driver HAL pour un bus et adresse donnee
  HalDriver* _getDriver(HardwareBus bus, uint8_t hwAddress);
  PCA9685Driver* _getPcaDriver(uint8_t hwAddress);
};

#endif // ACTUATOR_FACTORY_H
