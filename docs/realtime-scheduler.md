# Scheduler temps reel

## Objectifs
- Latence bout-en-bout stable: cible 1-2 ms.
- Tick scheduler: 1 kHz (`SCHEDULER_TICK_HZ`).
- Jitter reduit avec timer hardware ESP32.
- Execution non bloquante.

## Modele
1. Reception MIDI (rtpMIDI WiFi).
2. Event processor compile/evalue pipeline.
3. Generation de commandes horodatees (microsecondes).
4. Scheduler dispatch quand `execute_at <= now`.
5. Actuator manager execute la commande sur le driver adapte.

## Decoupage taches (FreeRTOS dual-core)
- **Core 1 (RT)**: MIDI update + scheduler update + watchdog actuateurs.
- **Core 0 (App)**: boucle applicative + web server + loop engine.

## Flag ISR timer
- `std::atomic<bool> schedulerTimerFired` — signal du timer hardware vers la tache RT.
- Utilise `memory_order_release` (ecriture ISR) / `memory_order_acquire` (lecture tache).

## Queue statique
- Ring buffer 128 commandes (`ActuatorCommand`).
- Zero allocation dynamique dans le chemin critique.
- Chaque commande horodatee en microsecondes.

## Securite
- Watchdog periodique des actionneurs (timeout configurable par type).
- Limites max d'activation simultanee (`MAX_CONCURRENT_ACTIVE`).
- Cache `_activeCount` O(1) mis a jour apres chaque dispatch/watchdog.
- Cooldown par actionneur (configurable).

## Concurrence I2C
- Mutex FreeRTOS global (`g_i2cMutex`) pour tous les acces bus I2C.
- Timeout acquisition mutex: 5ms (evite deadlock sur le chemin RT).
- Recovery automatique apres 10 erreurs I2C consecutives par driver.

## Loop Engine (sequenceur)
- Tick fractionnaire pour eviter le drift de timing.
  - `_tickRemainderPerTick` (x1000) accumule le reste de la division `60000000 / (bpm * ppq)`.
  - Compensation appliquee a chaque tick via `_tickRemainderAccum`.
- Quantize avec protection division par zero et clamp aux limites de boucle.
- Persistence automatique des loops dans LittleFS.

## Metriques exposees
- `/api/status` : jitter, queue usage, actionneurs actifs, compteurs CC.
- Alarmes UI : overflow scheduler, CC dropped, jitter eleve, memoire basse.
