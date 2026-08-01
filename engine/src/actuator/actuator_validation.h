#ifndef ACTUATOR_VALIDATION_H
#define ACTUATOR_VALIDATION_H

#include "../core/types.h"

// ============================================================================
// Shared actuator config validation (P1 #15)
// ============================================================================
// Single source of truth used by every path that accepts an ActuatorConfig:
//   - the REST API (create/update)
//   - ActuatorFactory::addConfig() — the funnel for LittleFS load, V4
//     migration and template instantiation
// so a corrupted or hand-edited config file can no longer inject an out-of-range
// GPIO, a bad type/bus pairing or inconsistent parameters.
//
// Returns true if valid; on failure sets errMsg to a static reason string.
bool validateActuatorConfig(const ActuatorConfig& cfg, const char*& errMsg);

// ----------------------------------------------------------------------------
// ESP32 (classic / WROOM-32) GPIO capability helpers (review #13)
// ----------------------------------------------------------------------------
// The bare 0..39 range is not enough: some numbers are not bonded out, GPIO
// 6-11 drive the SPI flash, and GPIO 34-39 are input-only. Configuring an
// output actuator on such a pin silently produces no signal (or corrupts flash).
bool esp32GpioExists(uint8_t pin);          // pin is a real, bonded-out GPIO
bool esp32GpioIsFlashReserved(uint8_t pin); // 6..11 (SPI flash)
bool esp32GpioIsInputOnly(uint8_t pin);     // 34..39 (no output driver)
bool esp32GpioCanOutput(uint8_t pin);       // usable as a digital/PWM output
bool esp32GpioCanInput(uint8_t pin);        // usable as a digital input

#endif // ACTUATOR_VALIDATION_H
