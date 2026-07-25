// Minimal FreeRTOS portmacro shim for the native (host) unit-test environment.
// The spinlock is a no-op on the single-threaded test host — the tests exercise
// the queue's ordering/transactional logic, not real cross-core contention.
#pragma once

typedef int portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux)      ((void)(mux))
#define portEXIT_CRITICAL(mux)       ((void)(mux))
