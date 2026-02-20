#ifndef COMMAND_QUEUE_H
#define COMMAND_QUEUE_H

#include "../core/types.h"
#include "../core/config.h"

// ============================================================================
// CommandQueue - File circulaire statique thread-safe
// ============================================================================
// Ring buffer pour les commandes actuateur.
// Les commandes sont inserees dans l'ordre temporel croissant.
// Pas de tri dynamique necessaire.
// Aucune allocation dynamique.
// Thread-safe: spinlock protege les acces multi-core (push from Core 0 + Core 1).
// ============================================================================

class CommandQueue {
public:
  CommandQueue();

  // Ajouter une commande dans la queue
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
  mutable portMUX_TYPE _mux;  // C1 fix: spinlock for multi-producer safety on dual-core ESP32
};

#endif // COMMAND_QUEUE_H
