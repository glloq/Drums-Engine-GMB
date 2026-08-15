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

// ----------------------------------------------------------------------------
// Pins reserved by Drums Engine itself
// ----------------------------------------------------------------------------
// The capability helpers above only describe the SoC. They say nothing about
// the pins this firmware already drives (I2C bus, status LED, BOOT button, I2S
// microphone, default LED-strip data lines), so an actuator could previously be
// configured onto GPIO 21 and silently kill the whole I2C bus.
//
// This table is the single source of truth: the validator rejects the exclusive
// entries, and /api/system/info exports the whole list so the "Cablage" page
// documents exactly the same pins instead of hardcoding its own copy.
struct PinReservation {
  uint8_t     pin;
  const char* function;   // stable identifier, e.g. "i2c_sda"
  const char* label;      // human-readable, shown by the UI
  bool        exclusive;  // true: an actuator may never take this pin
};

// All reservations, in pin order. `count` receives the entry count.
const PinReservation* esp32PinReservations(uint8_t& count);

// The reservation covering `pin`, or nullptr when the pin is free.
const PinReservation* esp32PinReservation(uint8_t pin);

// ----------------------------------------------------------------------------
// Hardware resource conflict detection (review #13)
// ----------------------------------------------------------------------------
// True when configs `a` and `b` fight over the same physical resource: an ESP32
// GPIO (output pin, end stop, optical sensor, motor direction pin), an MCP23017
// pin or a PCA9685 channel.
//
// Lives here rather than in ActuatorFactory so both the create and the update
// path share it, and so it can be exercised by the native unit tests without
// dragging in the HAL drivers.
bool actuatorConfigsConflict(const ActuatorConfig& a, const ActuatorConfig& b);

// Does `c` occupy the physical ESP32 GPIO `pin` (output pin on a GPIO/LEDC bus,
// end stop, optical sensor or motor direction pin)? Used to check an actuator
// against non-actuator consumers of the same pin — LED strips, the I2S
// microphone — which actuatorConfigsConflict() cannot express.
bool actuatorConfigUsesGpio(const ActuatorConfig& c, uint8_t pin);

#endif // ACTUATOR_VALIDATION_H
