# Architecture technique

## Couches logiques
Le moteur suit une architecture descendante stricte:

1. **MIDI Layer**
2. **Event Engine**
3. **Compiled Pipeline Tables**
4. **Scheduler**
5. **Actuator Engine**
6. **HAL Drivers**

Aucune couche basse ne dépend d'une couche haute.

## Flux d'exécution
`MIDI event -> pipeline compilé -> ActuatorCommand horodatée -> scheduler -> actuator -> driver hardware`

## Organisation du code
- `engine/src/core`: constantes globales, types, état global.
- `engine/src/hal`: abstractions matérielles (MCP23017, PCA9685, GPIO, LEDC).
- `engine/src/actuator`: classes d'actionneurs + manager + factory.
- `engine/src/scheduler`: queue de commandes et exécution temporelle.
- `engine/src/event`: compilation et exécution des blocs pipeline.
- `engine/src/midi`: réception et traitement MIDI.
- `engine/src/instrument`: définition et mapping des instruments.
- `engine/src/storage`: persistence/migration LittleFS.
- `engine/src/web`: API REST/WebSocket.

## Contraintes design
- Limites mémoire connues au compile-time (`MAX_*`).
- Pas d'allocation dynamique dans les chemins critiques.
- Timestamping microsecondes et dispatch non bloquant.
- Sécurité actionneurs (watchdog, cooldowns, limites).
