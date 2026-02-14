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
  ActuatorFactory(ActuatorManager* manager,
                  MCP23017Driver* mcpDrivers, uint8_t mcpCount,
                  PCA9685Driver* pcaDrivers, uint8_t pcaCount,
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

  // Reconstruire tous les actionneurs depuis le pool de configs
  uint8_t rebuildAll();

private:
  ActuatorManager* _manager;

  // HAL drivers references
  MCP23017Driver* _mcpDrivers;
  uint8_t _mcpCount;
  PCA9685Driver* _pcaDrivers;
  uint8_t _pcaCount;
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
