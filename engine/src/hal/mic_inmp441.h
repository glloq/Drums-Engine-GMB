#ifndef MIC_INMP441_H
#define MIC_INMP441_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "../core/config.h"

// ============================================================================
// MicINMP441 - INMP441 I2S Microphone Driver
// ============================================================================
// Provides sound level monitoring and trigger detection for latency
// measurement. Only initialized if hardware is detected (valid I2S reads).
// Runs on Core 0 alongside the web server.
// ============================================================================

class MicINMP441 {
public:
  MicINMP441();

  // Initialize I2S and detect if mic is connected
  // Returns true if INMP441 responds with non-zero samples
  bool begin();

  // Release I2S resources
  void end();

  // Read current peak amplitude (call periodically, ~1kHz)
  // Returns peak absolute sample value since last call
  uint32_t readPeak();

  // Get current RMS level (0-100 scaled)
  uint8_t getLevel() const { return _level; }

  // Is mic hardware detected and active?
  bool isConnected() const { return _connected; }

  // --- Trigger detection for latency measurement ---

  // Arm trigger: next peak above threshold will record timestamp
  void armTrigger(uint16_t threshold);

  // Was trigger fired since last arm?
  bool isTriggered() const { return _triggered; }

  // Timestamp (micros) when trigger fired
  uint32_t getTriggerTimestamp() const { return _triggerTimestampUs; }

  // Peak value that caused the trigger
  uint32_t getTriggerPeak() const { return _triggerPeak; }

  // Disarm trigger
  void disarmTrigger();

private:
  bool _connected;
  uint8_t _level;           // Scaled 0-100
  uint32_t _peakAccum;      // Peak since last readPeak()

  // Trigger state
  volatile bool _triggered;
  volatile uint32_t _triggerTimestampUs;
  volatile uint32_t _triggerPeak;
  uint16_t _triggerThreshold;
  bool _triggerArmed;
};

#endif // MIC_INMP441_H
