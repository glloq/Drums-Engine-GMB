#ifndef COMMAND_QUEUE_H
#define COMMAND_QUEUE_H

#include "../core/types.h"
#include "../core/config.h"
#include <freertos/portmacro.h>

// ============================================================================
// CommandQueue - File statique triee par date d'execution, protegee par spinlock
// ============================================================================
// Les commandes sont maintenues triees par `execute_at` croissant (compare avec
// prise en compte du wraparound de micros()), de sorte que la tete est toujours
// la commande la plus urgente. Cela corrige le blocage FIFO ou une commande
// lointaine en tete empechait une commande immediate placee derriere de partir
// (P1 #4).
//
// Thread safety (P0 #5) : TOUTES les mutations ET lectures d'index sont
// protegees par un unique spinlock `_mux`. push() est appele depuis Core 0
// (web/loop) et Core 1 (MIDI/EventProcessor) ; pop()/hasDue() depuis Core 1
// (scheduler). Cette queue n'est PAS lock-free.
//
// Insertion transactionnelle (P0 #7) : pushPair() insere ON+OFF sous un seul
// verrou, tout ou rien, pour ne jamais laisser un actionneur active sans son
// OFF programme.
//
// Aucune allocation dynamique.
// ============================================================================

class CommandQueue {
public:
  CommandQueue();

  // Ajouter une commande (thread-safe). Inseree a sa position triee.
  // Retourne true si ajout reussi, false si queue pleine (les commandes OFF
  // evincent la commande la moins urgente plutot que d'etre rejetees).
  bool push(const ActuatorCommand& cmd);

  // Inserer un couple ON/OFF de facon transactionnelle (P0 #7).
  // Les deux commandes sont inserees, ou aucune. Reserve les 2 emplacements
  // avant d'accepter. Retourne false (rien insere) si moins de 2 places libres.
  bool pushPair(const ActuatorCommand& on, const ActuatorCommand& off);

  // Retirer la commande la plus urgente (tete). Retourne false si vide.
  bool pop(ActuatorCommand& cmd);

  // Regarder la tete sans la retirer.
  bool peek(ActuatorCommand& cmd) const;

  // Vrai si la tete est prete a executer (execute_at <= now, wraparound-safe).
  bool hasDue(uint32_t now) const;

  // Nombre d'elements dans la queue.
  uint16_t count() const;

  // Queue vide / pleine ?
  bool isEmpty() const;
  bool isFull() const;

  // Vider la queue (atomique vis-a-vis producteurs et consommateur, P0 #5).
  void clear();

  // Statistiques
  uint32_t getOverflowCount() const { return _overflowCount; }
  float usage() const;

private:
  ActuatorCommand _buffer[COMMAND_QUEUE_SIZE];  // trie par execute_at croissant
  uint16_t _count;
  uint32_t _overflowCount;
  mutable portMUX_TYPE _mux;    // Spinlock protegeant TOUT l'etat

  // Wraparound-aware: vrai si `a` s'execute strictement avant `b`.
  // Valide tant que l'etendue des timestamps vivants < 2^31 us (~35 min),
  // largement respecte (horizon de quelques secondes).
  static inline bool _before(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
  }

  // Insere en position triee. Doit etre appele avec _mux tenu et _count < SIZE.
  void _insertLocked(const ActuatorCommand& cmd);
};

#endif // COMMAND_QUEUE_H
