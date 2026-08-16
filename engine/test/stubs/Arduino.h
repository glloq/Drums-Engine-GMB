// Minimal Arduino.h shim for the native (host) unit-test environment.
// Provides only what the pure-logic headers under test need: fixed-width
// integer types and the C string/memory helpers. No hardware, no Serial.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// Arduino core helpers used by the pure-logic modules under test. Same
// semantics as the real ones; no hardware involved.
inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
  if (inMax == inMin) return outMin;
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

// Time sources. The engine's decision paths take their timestamps from the MIDI
// event, so a fixed clock is enough here — and it keeps the tests deterministic.
// Only COND_RANDOM consumes micros() directly, as a seed.
inline uint32_t micros() { return 0; }
inline uint32_t millis() { return 0; }
