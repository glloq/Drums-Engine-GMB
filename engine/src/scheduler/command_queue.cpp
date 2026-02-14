#include "command_queue.h"

CommandQueue::CommandQueue()
  : _head(0), _tail(0), _overflowCount(0) {}

bool CommandQueue::push(const ActuatorCommand& cmd) {
  uint16_t nextHead = (_head + 1) % COMMAND_QUEUE_SIZE;

  if (nextHead == _tail) {
    // Queue pleine
    _overflowCount++;
    DBGF("[Queue] OVERFLOW! (count=%lu)\n", _overflowCount);
    return false;
  }

  _buffer[_head] = cmd;
  _head = nextHead;
  return true;
}

bool CommandQueue::pop(ActuatorCommand& cmd) {
  if (_tail == _head) return false;  // Queue vide

  cmd = _buffer[_tail];
  _tail = (_tail + 1) % COMMAND_QUEUE_SIZE;
  return true;
}

bool CommandQueue::peek(ActuatorCommand& cmd) const {
  if (_tail == _head) return false;
  cmd = _buffer[_tail];
  return true;
}

bool CommandQueue::hasDue(uint32_t now) const {
  if (_tail == _head) return false;
  return ((int32_t)(now - _buffer[_tail].execute_at) >= 0);
}

uint16_t CommandQueue::count() const {
  if (_head >= _tail) {
    return _head - _tail;
  }
  return COMMAND_QUEUE_SIZE - _tail + _head;
}

bool CommandQueue::isEmpty() const {
  return _head == _tail;
}

bool CommandQueue::isFull() const {
  return ((_head + 1) % COMMAND_QUEUE_SIZE) == _tail;
}

void CommandQueue::clear() {
  _head = 0;
  _tail = 0;
}

float CommandQueue::usage() const {
  return (float)count() / COMMAND_QUEUE_SIZE * 100.0f;
}
