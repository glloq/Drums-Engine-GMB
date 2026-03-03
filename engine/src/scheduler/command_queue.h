#ifndef COMMAND_QUEUE_H
#define COMMAND_QUEUE_H

#include "../core/types.h"
#include "../core/config.h"
#include <freertos/portmacro.h>

// ============================================================================
// CommandQueue - File circulaire statique avec spinlock
// ============================================================================
// Ring buffer pour les commandes actuateur.
// Les commandes sont inserees dans l'ordre temporel croissant.
// Pas de tri dynamique necessaire.
// Aucune allocation dynamique.
//
// Thread safety: push() est appele depuis Core 0 (web test, loop playback)
// ET Core 1 (EventProcessor MIDI). Un spinlock protege les ecritures.
// pop() est appele uniquement depuis Core 1 (scheduler tick).
// ============================================================================

class CommandQueue {
public:
  CommandQueue();

  // Ajouter une commande dans la queue (thread-safe, spinlock protected)
  // Retourne true si ajout reussi, false si queue pleine
  bool push(const ActuatorCommand& cmd);

  // Retirer la prochaine commande
  // Retourne true si une commande est disponible
  bool pop(ActuatorCommand& cmd);

  // Regarder la prochaine commande sans la retirer
  bool peek(ActuatorCommand& cmd) const;

  // Verifier si des commandes sont pretes a executer
  bool hasDue(uint32_t now) const;

  // Nombre d'elements dans la queue
  uint16_t count() const;

  // Queue vide ?
  bool isEmpty() const;

  // Queue pleine ?
  bool isFull() const;

  // Vider la queue
  void clear();

  // Statistiques
  uint32_t getOverflowCount() const { return _overflowCount; }
  float usage() const;

private:
  ActuatorCommand _buffer[COMMAND_QUEUE_SIZE];
  volatile uint16_t _head;  // Position d'ecriture
  volatile uint16_t _tail;  // Position de lecture
  uint32_t _overflowCount;
  portMUX_TYPE _pushMux;    // Spinlock pour push() multi-core
};

#endif // COMMAND_QUEUE_H
