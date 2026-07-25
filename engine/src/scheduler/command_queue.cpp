#include "command_queue.h"

CommandQueue::CommandQueue()
  : _count(0), _overflowCount(0), _mux(portMUX_INITIALIZER_UNLOCKED) {}

// Insert keeping the buffer sorted ascending by execute_at.
// Caller must hold _mux and guarantee _count < COMMAND_QUEUE_SIZE.
void CommandQueue::_insertLocked(const ActuatorCommand& cmd) {
  // Find insertion index: first element that executes strictly AFTER cmd.
  // Using >= keeps insertion stable (FIFO among equal timestamps).
  uint16_t i = _count;
  while (i > 0 && _before(cmd.execute_at, _buffer[i - 1].execute_at)) {
    _buffer[i] = _buffer[i - 1];
    i--;
  }
  _buffer[i] = cmd;
  _count++;
}

bool CommandQueue::push(const ActuatorCommand& cmd) {
  portENTER_CRITICAL(&_mux);

  if (_count >= COMMAND_QUEUE_SIZE) {
    // Full. OFF commands are safety-critical and must not be dropped:
    // evict the least-urgent command (the tail, greatest execute_at) to
    // make room. Non-OFF commands are rejected.
    if ((CommandType)cmd.command_type == CommandType::OFF) {
      _count--;  // drop tail (latest / least urgent)
      _insertLocked(cmd);
      _overflowCount++;
      portEXIT_CRITICAL(&_mux);
      return true;
    }
    _overflowCount++;
    portEXIT_CRITICAL(&_mux);
    DBGF("[Queue] OVERFLOW! (count=%lu)\n", _overflowCount);
    return false;
  }

  _insertLocked(cmd);
  portEXIT_CRITICAL(&_mux);
  return true;
}

bool CommandQueue::pushPair(const ActuatorCommand& on, const ActuatorCommand& off) {
  portENTER_CRITICAL(&_mux);

  // Reserve two slots up-front: insert both or neither so an actuator can
  // never be turned ON without its scheduled OFF (P0 #7).
  if (_count > COMMAND_QUEUE_SIZE - 2) {
    _overflowCount++;
    portEXIT_CRITICAL(&_mux);
    DBGF("[Queue] OVERFLOW (pair rejected, count=%lu)\n", _overflowCount);
    return false;
  }

  _insertLocked(on);
  _insertLocked(off);
  portEXIT_CRITICAL(&_mux);
  return true;
}

bool CommandQueue::pop(ActuatorCommand& cmd) {
  portENTER_CRITICAL(&_mux);
  if (_count == 0) {
    portEXIT_CRITICAL(&_mux);
    return false;
  }
  cmd = _buffer[0];
  // Shift the remaining elements down (n <= 128, trivial cost).
  for (uint16_t i = 1; i < _count; i++) {
    _buffer[i - 1] = _buffer[i];
  }
  _count--;
  portEXIT_CRITICAL(&_mux);
  return true;
}

bool CommandQueue::peek(ActuatorCommand& cmd) const {
  portENTER_CRITICAL(&_mux);
  if (_count == 0) {
    portEXIT_CRITICAL(&_mux);
    return false;
  }
  cmd = _buffer[0];
  portEXIT_CRITICAL(&_mux);
  return true;
}

bool CommandQueue::hasDue(uint32_t now) const {
  portENTER_CRITICAL(&_mux);
  bool due = (_count > 0) && ((int32_t)(now - _buffer[0].execute_at) >= 0);
  portEXIT_CRITICAL(&_mux);
  return due;
}

uint16_t CommandQueue::count() const {
  portENTER_CRITICAL(&_mux);
  uint16_t c = _count;
  portEXIT_CRITICAL(&_mux);
  return c;
}

bool CommandQueue::isEmpty() const {
  portENTER_CRITICAL(&_mux);
  bool e = (_count == 0);
  portEXIT_CRITICAL(&_mux);
  return e;
}

bool CommandQueue::isFull() const {
  portENTER_CRITICAL(&_mux);
  bool f = (_count >= COMMAND_QUEUE_SIZE);
  portEXIT_CRITICAL(&_mux);
  return f;
}

void CommandQueue::clear() {
  portENTER_CRITICAL(&_mux);
  _count = 0;
  portEXIT_CRITICAL(&_mux);
}

float CommandQueue::usage() const {
  portENTER_CRITICAL(&_mux);
  float u = (float)_count / COMMAND_QUEUE_SIZE * 100.0f;
  portEXIT_CRITICAL(&_mux);
  return u;
}
