#ifndef LED_STRIP_DRIVER_H
#define LED_STRIP_DRIVER_H

#include <Arduino.h>
#include "../core/config.h"
#include "../core/types.h"

// ============================================================================
// LedStripDriver - WS2812B via ESP32 RMT (hardware timing)
// ============================================================================
// Gere jusqu'a 4 strips physiques, chacun sur son propre canal RMT.
// Le buffer de pixels est partage : chaque strip a un offset dans le buffer.
// Pas de dependance FastLED — utilise directement le peripherique RMT ESP32
// via la lib Adafruit NeoPixel (legere, stable, RMT natif).
// ============================================================================

class LedStripDriver {
public:
  LedStripDriver();

  // Initialiser un strip (alloue le buffer RMT interne)
  bool beginStrip(uint8_t stripIdx, const LedStripConfig& config);

  // Eteindre et liberer un strip
  void endStrip(uint8_t stripIdx);

  // Ecrire la couleur d'un pixel sur un strip
  void setPixel(uint8_t stripIdx, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b);
  void setPixel(uint8_t stripIdx, uint16_t pixel, const RgbColor& color);

  // Eteindre tous les pixels d'un strip
  void clearStrip(uint8_t stripIdx);

  // Eteindre tous les strips
  void clearAll();

  // Envoyer les donnees au hardware (appeler apres setPixel)
  void show(uint8_t stripIdx);

  // Envoyer les donnees de tous les strips actifs
  void showAll();

  // Appliquer la luminosite max du strip (clampe les valeurs)
  void setStripBrightness(uint8_t stripIdx, uint8_t brightness);

  // Nombre de pixels d'un strip
  uint16_t getPixelCount(uint8_t stripIdx) const;

  // Strip actif ?
  bool isActive(uint8_t stripIdx) const;

private:
  // Buffer interne : 3 octets (RGB) par pixel, pour tous les strips
  // Chaque strip a son propre sous-buffer
  struct StripState {
    uint8_t* buffer;       // Buffer RGB (3 * ledCount)
    uint16_t ledCount;
    uint8_t gpioPin;
    uint8_t brightness;    // Max brightness 0-255
    bool active;
  };
  StripState _strips[LED_MAX_STRIPS];

  // RMT transmission helper
  void _rmtSend(uint8_t stripIdx);
};

#endif // LED_STRIP_DRIVER_H
