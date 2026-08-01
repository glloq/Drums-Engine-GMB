#ifndef INSTRUMENT_VALIDATION_H
#define INSTRUMENT_VALIDATION_H

#include "../core/types.h"

// ============================================================================
// Shared instrument config validation (review #14)
// ============================================================================
// Single source of truth used by the REST API (create/update) AND the LittleFS
// load path (InstrumentManager::fromJson), so a hand-edited config file can no
// longer inject an out-of-range note/channel, an invalid command/value source,
// a dangling actuator reference or an incompatible command/actuator pairing.
//
// The validator needs to resolve actuator ids to their config; callers supply a
// lookup callback (the API uses the factory's config pool, the load path uses
// the actuator manager). A captureless lambda converts to this function pointer.
//
// Note: like the actuator validator, this MUTATES cfg to normalize it (swaps
// inverted min/max ranges). Returns true if valid; on failure sets errMsg.
typedef const ActuatorConfig* (*ActuatorLookupFn)(void* ctx, uint8_t actuatorId);

bool validateInstrumentConfig(InstrumentConfig& cfg, const char*& errMsg,
                              ActuatorLookupFn lookup, void* ctx);

#endif // INSTRUMENT_VALIDATION_H
