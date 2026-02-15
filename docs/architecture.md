# Architecture technique

## Couches logiques
Le moteur suit une architecture descendante:

1. **MIDI Layer**
2. **Event Engine**
3. **Compiled Lookup Tables** (notes + routes CC)
4. **Scheduler**
5. **Actuator Engine**
6. **HAL Drivers**

## Flux d'exécution
- **NoteOn/NoteOff**: `MIDI -> lookup note->pipeline -> ActionStep[] -> scheduler -> actuator`
- **CC**: `MIDI CC -> update global vars -> lookup cc_routes -> scheduler -> actuator`
- **Pitch Bend (pipeline mode)**: converti en valeur 0..127 puis routé via table CC virtuelle (`CC127`).

## Structures clés
- `InstrumentConfig`
  - `noteOnActions[]`, `noteOffActions[]`, `ccBindings[]`, `retriggerMode`
- `CompiledPipeline`
  - actions/bindings compilées + `retrigger_mode`
- `PipelineLookup`
  - `note_to_pipeline[128]`
  - table CC compilée (`cc_routes`, `cc_to_first`, `cc_to_count`)

## Temps réel et sécurité
- Scheduler horodaté en microsecondes.
- Queue statique, sans allocation dynamique en chemin critique.
- Anti-flood CC (fenêtre 5ms par numéro de CC).
- Watchdog actuateurs périodique via tâche RT.
- Cooldown et timeout selon type d’actionneur.

## Composants de code
- `engine/src/event`: compilation lookup + traitement MIDI runtime
- `engine/src/scheduler`: ordonnanceur commandes
- `engine/src/actuator`: comportements matériels (solénoïde/servo/moteur)
- `engine/src/web`: API REST + UI static + endpoints debug

## État d’implémentation
- Modèle actions/CC: en production (MVP)
- Routage CC compilé: en production (MVP)
- UI éditeur instruments complexes: en production (MVP)
- Comportements spécialisés Phase 5: partiels


- Actuateur `MOTOR_OPTICAL`: pilotage moteur + suivi capteur optique (front montant) dans le watchdog d'actuateur.
