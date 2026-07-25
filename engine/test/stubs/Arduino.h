// Minimal Arduino.h shim for the native (host) unit-test environment.
// Provides only what the pure-logic headers under test need: fixed-width
// integer types and the C string/memory helpers. No hardware, no Serial.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
