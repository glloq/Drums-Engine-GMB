#include "command_queue.h"

CommandQueue::CommandQueue()
  : _head(0), _tail(0), _overflowCount(0), _mux(portMUX_INITIALIZER_UNLOCKED) {}

bool CommandQueue::push(const ActuatorCommand& cmd) {
  // C1 fix: Spinlock protects multi-producer push from Core 0 + Core 1
  portENTER_CRITICAL(&_mux);

  uint16_t nextHead = (_head + 1) % COMMAND_QUEUE_SIZE;

  if (nextHead == _tail) {
    // H3 fix: OFF commands must not be dropped (safety critical).
    // Drop the oldest command to make room for OFF.
    if (cmd.command_type == (uint8_t)CommandType::OFF) {
      _tail = (_tail + 1) % COMMAND_QUEUE_SIZE;  // Evict oldest
      _buffer[_head] = cmd;
      _head = nextHead;
      _overflowCount++;
      portEXIT_CRITICAL(&_mux);
      return true;
    }
    // Queue pleine
    _overflowCount++;
    portEXIT_CRITICAL(&_mux);
    DBGF("[Queue] OVERFLOW! (count=%lu)\n", _overflowCount);
    return false;
  }

  _buffer[_head] = cmd;
  _head = nextHead;
  portEXIT_CRITICAL(&_mux);
  return true;
}

bool CommandQueue::pop(ActuatorCommand& cmd) {
  portENTER_CRITICAL(&_mux);
  if (_tail == _head) {
    portEXIT_CRITICAL(&_mux);
    return false;  // Queue vide
  }

  cmd = _buffer[_tail];
  _tail = (_tail + 1) % COMMAND_QUEUE_SIZE;
  portEXIT_CRITICAL(&_mux);
  return true;
}

bool CommandQueue::peek(ActuatorCommand& cmd) const {
  portENTER_CRITICAL(&_mux);
  if (_tail == _head) {
    portEXIT_CRITICAL(&_mux);
    return false;
  }
  cmd = _buffer[_tail];
  portEXIT_CRITICAL(&_mux);
  return true;
}

bool CommandQueue::hasDue(uint32_t now) const {
  portENTER_CRITICAL(&_mux);
  if (_tail == _head) {
    portEXIT_CRITICAL(&_mux);
    return false;
  }
  bool due = ((int32_t)(now - _buffer[_tail].execute_at) >= 0);
  portEXIT_CRITICAL(&_mux);
  return due;
}

uint16_t CommandQueue::count() const {
  portENTER_CRITICAL(&_mux);
  uint16_t c;
  if (_head >= _tail) {
    c = _head - _tail;
  } else {
    c = COMMAND_QUEUE_SIZE - _tail + _head;
  }
  portEXIT_CRITICAL(&_mux);
  return c;
}

bool CommandQueue::isEmpty() const {
  portENTER_CRITICAL(&_mux);
  bool empty = (_head == _tail);
  portEXIT_CRITICAL(&_mux);
  return empty;
}

bool CommandQueue::isFull() const {
  portENTER_CRITICAL(&_mux);
  bool full = ((_head + 1) % COMMAND_QUEUE_SIZE) == _tail;
  portEXIT_CRITICAL(&_mux);
  return full;
}

void CommandQueue::clear() {
  portENTER_CRITICAL(&_mux);
  _head = 0;
  _tail = 0;
  portEXIT_CRITICAL(&_mux);
}

float CommandQueue::usage() const {
  return (float)count() / COMMAND_QUEUE_SIZE * 100.0f;
}
