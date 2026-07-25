#ifndef ACTUATOR_H
#define ACTUATOR_H

#include "../core/types.h"
#include "../core/config.h"

// ============================================================================
// Actuator - Classe de base abstraite
// ============================================================================
// Chaque type d'actionneur herite de cette classe.
// Ne connait pas le MIDI. Recoit uniquement des ActuatorCommand.
//
// Securite integree :
//   - Cooldown entre activations (anti-rebond thermique)
//   - Tracking activation (pour watchdog timeout)
//   - Interface commune pour overload protection
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

  // --- Safety ---

  // Verifier si l'actionneur est actif (allume/en mouvement)
  bool isActive() const { return _active; }

  // Timestamp du debut d'activation
  uint32_t getActivationStart() const { return _activationStartUs; }

  // Verifier le timeout watchdog (thermique / duree max). Appele a basse
  // frequence (~10 Hz). Retourne true si l'actionneur a ete force a s'arreter.
  virtual bool checkTimeout(uint32_t nowUs) { return false; }

  // Verifier les fins de course. Appele a HAUTE frequence (~1 kHz) — separe du
  // watchdog thermique pour reagir vite a une butee (review #5). Par defaut
  // sans effet (seuls les moteurs a fin de course l'implementent).
  // Retourne true si l'actionneur a ete arrete suite a une butee.
  virtual bool checkEndStops(uint32_t nowUs) { return false; }

protected:
  ActuatorConfig _config;
  bool _active = false;
  uint32_t _activationStartUs = 0;

  // Verifier le cooldown. Retourne true si l'activation est permise.
  bool checkCooldown(uint32_t nowUs) {
    if (_config.cooldownUs == 0) return true;
    uint32_t cooldownReal = (uint32_t)_config.cooldownUs * 100; // cooldownUs stocke en /100
    uint32_t elapsed = nowUs - _config.lastActivation;
    if (elapsed < cooldownReal) {
      return false; // Encore en cooldown
    }
    return true;
  }

  // Marquer l'activation
  void markActivation(uint32_t nowUs) {
    _config.lastActivation = nowUs;
    _active = true;
    _activationStartUs = nowUs;
  }

  // Marquer la desactivation
  void markDeactivation() {
    _active = false;
  }
};

#endif // ACTUATOR_H
