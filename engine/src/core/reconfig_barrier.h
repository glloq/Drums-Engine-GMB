#ifndef ENGINE_RECONFIG_BARRIER_H
#define ENGINE_RECONFIG_BARRIER_H

#include <Arduino.h>
#include <atomic>

// ============================================================================
// ReconfigBarrier - reconfiguration sure entre Core 0 et Core 1
// ============================================================================
// Le coeur temps reel (Core 1) lit en permanence deux structures partagees :
//
//   EventProcessor  -> PipelineLookup  (note -> pipeline -> actions)
//   Scheduler       -> ActuatorManager (_actuators[] / _idMap[])
//
// Les deux sont reecrites depuis Core 0 quand l'utilisateur modifie la
// configuration via l'API Web : compilePipelines() remet a zero la table de
// lookup, et ActuatorFactory::rebuildAll() appelle ActuatorManager::clearAll()
// puis reconstruit les objets actionneurs dans les pools statiques.
//
// Sans synchronisation, Core 1 pouvait donc dispatcher une commande vers un
// Actuator en train d'etre reconstruit, ou lire un pipeline a moitie recompile.
//
// Plutot que de dupliquer les pools (cout memoire important sur ESP32), on met
// le coeur temps reel en pause pendant la reconfiguration :
//
//        Core 0 (Web)                     Core 1 (RT)
//   ----------------------           ----------------------
//   acquire()                        boucle: requested() ?
//     _requested = true      ---->   oui -> stopAll + cancelAll
//     attend _parked                        park()
//                            <----          _parked = true
//   reconstruit les tables                  attend _requested == false
//   release()
//     _requested = false     ---->   reprend
//
// La pause dure typiquement 1 a 2 ticks FreeRTOS (~1-2 ms) : negligeable pour
// une edition de configuration, et le RT ne peut plus voir d'etat transitoire.
//
// Toutes les acquisitions viennent des handlers Web, qui s'executent sur la
// meme tache AsyncTCP et sont donc deja serialises entre eux ; le compteur
// _depth ne sert qu'a supporter l'imbrication (rebuildAll + compilePipelines
// dans la meme requete).
// ============================================================================

class ReconfigBarrier {
public:
  // --- Cote Core 1 (temps reel) ---

  // Une reconfiguration attend-elle que le RT se mette en pause ?
  bool requested() const { return _requested.load(std::memory_order_acquire); }

  // Se garer jusqu'a la fin de la reconfiguration. A n'appeler que depuis la
  // tache RT, apres avoir arrete les actionneurs.
  void park() {
    _parked.store(true, std::memory_order_release);
    while (_requested.load(std::memory_order_acquire)) {
      vTaskDelay(1);  // yield : ne bloque ni l'IDLE task ni le watchdog
    }
    _parked.store(false, std::memory_order_release);
  }

  // Signale que la tache RT tourne. Tant qu'elle n'a pas demarre (boot,
  // chargement de la config initiale) il n'y a personne pour se garer et
  // acquire() ne doit pas attendre.
  void setRtActive(bool active) { _rtActive.store(active, std::memory_order_release); }

  // --- Cote Core 0 (configuration) ---

  // Demande la pause du RT et attend qu'il soit gare. Retourne false si le RT
  // n'a pas repondu dans le delai imparti : l'appelant continue quand meme
  // (c'etait le comportement historique), mais l'anomalie est tracee.
  bool acquire(uint32_t timeoutMs = 250) {
    if (_depth > 0) { _depth++; return true; }   // deja acquis (imbrication)

    _depth = 1;
    if (!_rtActive.load(std::memory_order_acquire)) return true;  // RT pas demarre

    _requested.store(true, std::memory_order_release);
    uint32_t start = millis();
    while (!_parked.load(std::memory_order_acquire)) {
      if (millis() - start >= timeoutMs) return false;  // release() rendra la main
      vTaskDelay(1);
    }
    return true;
  }

  void release() {
    if (_depth == 0) return;
    if (--_depth == 0) _requested.store(false, std::memory_order_release);
  }

private:
  std::atomic<bool> _requested{false};
  std::atomic<bool> _parked{false};
  std::atomic<bool> _rtActive{false};
  uint8_t _depth = 0;   // touche uniquement depuis les handlers Web (serialises)
};

// Instance globale (definie dans main.cpp)
extern ReconfigBarrier g_reconfig;

// Garde RAII : garantit que release() est appele sur tous les chemins de sortie
// (y compris les `return` anticipes des handlers), sinon le RT resterait gare.
class ReconfigLock {
public:
  explicit ReconfigLock(uint32_t timeoutMs = 250) {
    _ok = g_reconfig.acquire(timeoutMs);
  }
  ~ReconfigLock() { g_reconfig.release(); }

  bool ok() const { return _ok; }

  ReconfigLock(const ReconfigLock&) = delete;
  ReconfigLock& operator=(const ReconfigLock&) = delete;

private:
  bool _ok;
};

#endif // ENGINE_RECONFIG_BARRIER_H
