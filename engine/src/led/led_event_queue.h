#ifndef LED_EVENT_QUEUE_H
#define LED_EVENT_QUEUE_H

#include "../core/config.h"
#include "../core/types.h"
#include <atomic>

// ============================================================================
// LedEventQueue - Ring buffer lock-free pour communication Core 1 → Core 0
// ============================================================================
// Meme pattern que CommandQueue du scheduler, adapte pour les events LED.
// Un seul producteur (Core 1 / EventProcessor), un seul consommateur (Core 0 / LedEngine).
// SPSC (Single Producer, Single Consumer) = pas besoin de mutex.
// ============================================================================

class LedEventQueue {
public:
  LedEventQueue() : _head(0), _tail(0), _overflow(0) {}

  // Producteur (Core 1): ajouter un event
  bool push(const LedEvent& event) {
    uint8_t next = (_head.load(std::memory_order_relaxed) + 1) % LED_EVENT_QUEUE_SIZE;
    if (next == _tail.load(std::memory_order_acquire)) {
      _overflow++;
      return false; // Queue pleine
    }
    _buffer[_head.load(std::memory_order_relaxed)] = event;
    _head.store(next, std::memory_order_release);
    return true;
  }

  // Consommateur (Core 0): retirer un event
  bool pop(LedEvent& event) {
    uint8_t tail = _tail.load(std::memory_order_relaxed);
    if (tail == _head.load(std::memory_order_acquire)) {
      return false; // Queue vide
    }
    event = _buffer[tail];
    _tail.store((tail + 1) % LED_EVENT_QUEUE_SIZE, std::memory_order_release);
    return true;
  }

  // Nombre d'events en attente
  uint8_t count() const {
    uint8_t h = _head.load(std::memory_order_acquire);
    uint8_t t = _tail.load(std::memory_order_acquire);
    return (h >= t) ? (h - t) : (LED_EVENT_QUEUE_SIZE - t + h);
  }

  bool isEmpty() const {
    return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire);
  }

  uint32_t getOverflowCount() const { return _overflow; }

private:
  LedEvent _buffer[LED_EVENT_QUEUE_SIZE];
  std::atomic<uint8_t> _head;
  std::atomic<uint8_t> _tail;
  uint32_t _overflow;
};

#endif // LED_EVENT_QUEUE_H
