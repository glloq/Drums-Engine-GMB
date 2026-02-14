#ifndef ACTUATOR_H
#define ACTUATOR_H

#include "../core/types.h"
#include "../core/config.h"

// ============================================================================
// Actuator - Classe de base abstraite
// ============================================================================
// Chaque type d'actionneur herite de cette classe.
// Ne connait pas le MIDI. Recoit uniquement des ActuatorCommand.
// ============================================================================

class Actuator {
public:
  virtual ~Actuator() {}

  // Initialiser l'actionneur
  virtual void init() = 0;

  // Executer une commande
  virtual void execute(const ActuatorCommand& cmd) = 0;

  // Couper l'actionneur (position repos)
  virtual void stop() = 0;

  // Configuration
  void setConfig(const ActuatorConfig& cfg) { _config = cfg; }
  const ActuatorConfig& getConfig() const { return _config; }
  ActuatorConfig& getConfig() { return _config; }

  uint8_t getId() const { return _config.id; }
  bool isEnabled() const { return _config.enabled; }

protected:
  ActuatorConfig _config;
};

#endif // ACTUATOR_H
