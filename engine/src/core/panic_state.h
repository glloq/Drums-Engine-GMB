#ifndef ENGINE_PANIC_STATE_H
#define ENGINE_PANIC_STATE_H

#include <atomic>

// ============================================================================
// Global emergency-stop latch (P0 #3 hardening)
// ============================================================================
// POST /api/panic sets this true BEFORE stopping everything. While it is true,
// every activation path across both cores refuses new commands (only OFF is
// still allowed through), closing the race where Core 1 could schedule/dispatch
// a fresh NoteOn between cancelAll() and stopAll() on Core 0.
//
// The latch stays set until an explicit POST /api/panic/reset re-arms the
// system — an emergency stop that auto-clears at the end of its handler would
// keep the same race window.
extern std::atomic<bool> g_panicActive;

#endif // ENGINE_PANIC_STATE_H
