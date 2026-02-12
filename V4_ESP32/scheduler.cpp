#include "scheduler.h"

Scheduler::Scheduler() {
  for (int i = 0; i < SCHEDULER_MAX_EVENTS; i++) {
    _events[i].active = false;
  }
}

int Scheduler::scheduleOnce(unsigned long delayMs, SchedulerCallback callback, void* data) {
  for (int i = 0; i < SCHEDULER_MAX_EVENTS; i++) {
    if (!_events[i].active) {
      _events[i].triggerTime = millis() + delayMs;
      _events[i].callback = callback;
      _events[i].data = data;
      _events[i].active = true;
      return i;
    }
  }
  DBGLN("[Scheduler] WARNING: Event pool full!");
  return -1;
}

void Scheduler::cancel(int slot) {
  if (slot >= 0 && slot < SCHEDULER_MAX_EVENTS) {
    _events[slot].active = false;
  }
}

void Scheduler::cancelAll() {
  for (int i = 0; i < SCHEDULER_MAX_EVENTS; i++) {
    _events[i].active = false;
  }
}

void Scheduler::update() {
  unsigned long now = millis();
  for (int i = 0; i < SCHEDULER_MAX_EVENTS; i++) {
    if (_events[i].active && (long)(now - _events[i].triggerTime) >= 0) {
      _events[i].active = false;
      _events[i].callback(_events[i].data);
    }
  }
}

uint8_t Scheduler::activeCount() const {
  uint8_t count = 0;
  for (int i = 0; i < SCHEDULER_MAX_EVENTS; i++) {
    if (_events[i].active) count++;
  }
  return count;
}

float Scheduler::usage() const {
  return (float)activeCount() / SCHEDULER_MAX_EVENTS * 100.0f;
}
