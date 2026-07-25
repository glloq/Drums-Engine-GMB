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

#endif // ACTUATOR_VALIDATION_H
