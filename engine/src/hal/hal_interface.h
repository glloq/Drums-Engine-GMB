#ifndef HAL_INTERFACE_H
#define HAL_INTERFACE_H

#include <Arduino.h>

// ============================================================================
// Hardware Abstraction Layer - Interface abstraite
// ============================================================================
// Aucune logique musicale ici. Uniquement des operations hardware bas niveau.
// Chaque driver implemente cette interface pour son bus specifique.
// ============================================================================

class HalDriver {
public:
  virtual ~HalDriver() {}

  // Initialiser le driver hardware
  virtual bool begin() = 0;

  // Ecrire une valeur digitale (HIGH/LOW)
  virtual void digitalWrite(uint8_t pin, bool value) = 0;

  // Ecrire une valeur PWM (0-4095 pour PCA9685, 0-255 pour LEDC)
  virtual void pwmWrite(uint8_t channel, uint16_t value) = 0;

  // Lire une entree digitale
  virtual bool digitalRead(uint8_t pin) = 0;

  // Verifier si le driver est pret
  virtual bool isReady() const = 0;

  // Nombre de canaux/pins disponibles
  virtual uint8_t channelCount() const = 0;
};

#endif // HAL_INTERFACE_H
