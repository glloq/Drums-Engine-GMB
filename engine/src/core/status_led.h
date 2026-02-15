#ifndef ENGINE_STATUS_LED_H
#define ENGINE_STATUS_LED_H

#include <Arduino.h>

// ============================================================================
// StatusLed - LED de statut sur GPIO 2 (LED intégrée ESP32)
// ============================================================================
// Patterns de clignotement :
//   BOOTING     : clignotement rapide (100ms)
//   AP_MODE     : clignotement lent (500ms)
//   CONNECTED   : allumée fixe
//   ERROR       : double clignotement rapide
// ============================================================================

#define STATUS_LED_PIN  2   // LED intégrée ESP32 DevKit
#define BOOT_BUTTON_PIN 0   // Bouton BOOT ESP32

enum class LedPattern : uint8_t {
  OFF = 0,
  BOOTING,      // Clignotement rapide 100ms
  AP_MODE,      // Clignotement lent 500ms
  CONNECTED,    // Allumée fixe
  ERROR_BLINK   // Double flash rapide
};

class StatusLed {
public:
  void begin() {
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    _pattern = LedPattern::BOOTING;
    _lastToggle = 0;
    _ledState = false;
    _blinkPhase = 0;
  }

  void setPattern(LedPattern pattern) {
    _pattern = pattern;
    _blinkPhase = 0;
  }

  LedPattern getPattern() const { return _pattern; }

  // Appeler régulièrement depuis une task (~chaque 10-50ms)
  void update() {
    uint32_t now = millis();

    switch (_pattern) {
      case LedPattern::OFF:
        _setLed(false);
        break;

      case LedPattern::BOOTING:
        // Clignotement rapide 100ms
        if (now - _lastToggle >= 100) {
          _lastToggle = now;
          _setLed(!_ledState);
        }
        break;

      case LedPattern::AP_MODE:
        // Clignotement lent 500ms
        if (now - _lastToggle >= 500) {
          _lastToggle = now;
          _setLed(!_ledState);
        }
        break;

      case LedPattern::CONNECTED:
        _setLed(true);
        break;

      case LedPattern::ERROR_BLINK:
        // Double flash: ON 100ms, OFF 100ms, ON 100ms, OFF 600ms
        if (now - _lastToggle >= _errorTiming[_blinkPhase]) {
          _lastToggle = now;
          _blinkPhase = (_blinkPhase + 1) % 4;
          _setLed(_blinkPhase == 0 || _blinkPhase == 2);
        }
        break;
    }
  }

private:
  LedPattern _pattern;
  uint32_t _lastToggle;
  bool _ledState;
  uint8_t _blinkPhase;

  static constexpr uint16_t _errorTiming[4] = {100, 100, 100, 600};

  void _setLed(bool on) {
    if (_ledState != on) {
      _ledState = on;
      digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
    }
  }
};

#endif // ENGINE_STATUS_LED_H
