#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Non-blocking event scheduler (ported from V3, adapted for ESP32)
// ============================================================================

typedef void (*SchedulerCallback)(void* data);

struct ScheduledEvent {
  unsigned long triggerTime;
  SchedulerCallback callback;
  void* data;
  bool active;
};

class Scheduler {
public:
  Scheduler();

  // Schedule a one-shot event
  // Returns slot index or -1 if pool full
  int scheduleOnce(unsigned long delayMs, SchedulerCallback callback, void* data = nullptr);

  // Cancel a scheduled event by slot index
  void cancel(int slot);

  // Cancel all events
  void cancelAll();

  // Must be called in loop() - triggers ready callbacks
  void update();

  // Stats
  uint8_t activeCount() const;
  uint8_t capacity() const { return SCHEDULER_MAX_EVENTS; }
  float usage() const;

private:
  ScheduledEvent _events[SCHEDULER_MAX_EVENTS];
};

#endif // SCHEDULER_H
