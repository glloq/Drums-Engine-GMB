# Etat des lieux actuel

**Derniere mise a jour**: 2026-02-18

## Resume
Le moteur supporte un modele instrument oriente actions multi-etapes (NoteOn/NoteOff) avec routage CC compile, editeur UI dedie, securite API complete, logging persistant, et protections concurrence dual-core.

## Capacites implementees

### Coeur temps reel
- Scheduler temps reel (timer hardware 1 kHz, queue statique 128 commandes, watchdog)
- Separation FreeRTOS dual-core: Core 1 (RT: MIDI + Scheduler) / Core 0 (Web + Loops)
- Zero allocation dynamique dans le chemin temps reel
- Flag ISR atomique (`std::atomic<bool>`)
- Cache `_activeCount` O(1) dans ActuatorManager

### Concurrence et fiabilite (sprint fiabilite)
- Spinlock `portENTER_CRITICAL` sur `_noteActive[128]` (partage dual-core)
- Mutex I2C global avec timeout 5ms
- Recovery I2C automatique (re-init driver apres 10 erreurs consecutives)
- Ecriture atomique LittleFS (`.tmp` + `rename`)
- WiFi STA non-bloquant (`vTaskDelay` au lieu de `delay`)

### Modele instrument
- `ActionStep` NoteOn/NoteOff (jusqu'a 4 etapes / evenement)
- `ValueSource`: velocity, fixed, CC var, inverted velocity
- Courbes velocity (lineaire, expo, log) et delays par etape
- Modes retrigger: IGNORE, RESET, STACK (compteur uint8_t)

### Routage CC
- Table compilee (`cc_routes`, `cc_to_first`, `cc_to_count`)
- Dispatch runtime anti-flood (5ms)
- Pitch bend via CC#126 virtuel (evite collision CC#127)
- Endpoint debug `/api/cc-routes` (avec compteur `dropped` + `max`)
- Diagnostics runtime CC exposes dans `/api/status`

### API & Templates
- CRUD complet: actuateurs, instruments, boucles
- Templates avec `slotHints` et validation stricte des slots
- Endpoint capabilities, diagnostics, scan I2C
- Test unitaire d'action, test instrument, test actuateur
- Sauvegarde/chargement LittleFS + migration V4

### Securite & Fiabilite
- **WiFiManager**: credentials depuis `/wifi.json`, mode AP fallback avec portail REST
- **ApiAuth**: Bearer token 16-hex, auto-genere, protege POST/PUT/DELETE
- **RateLimiter**: 30 req/s par IP, 8 clients, reponse 429
- **ErrorLog**: ring buffer 16 entrees + `/error.log` rotatif 8KB, API `/api/logs`

### Comportements actionneurs
- `SOLENOID_STRIKE`, `SOLENOID_HOLD`, `SOLENOID_MUTE`
- `SERVO_POSITION`, `SERVO_STRIKE`, `COMB_BRUSH`, `PITCH_BEND`
- `HIHAT_CONTROLLER`: controle CC#4 continu + splash 80ms
- `SERVO_MUTE`: toggle position mute/libre sur PULSE
- `MOTOR_OPTICAL_TRACK`: ISR hardware multi-instance (4 slots max)

### Loop Engine
- Sequenceur avec tick fractionnaire (anti-drift)
- Quantize avec guard division/0 et clamp aux limites
- Persistence automatique LittleFS sur create/update/delete
- Correction index courant apres suppression

### Architecture web serveur
- `web_server.cpp` scinde en 5 modules: core, routes_actuator, routes_instrument, routes_loop, routes_system
- Config JSON versionne (format v2 avec wrapper objet)

### UI Web
- SPA complete avec editeurs actuateurs, instruments, boucles
- Alarmes runtime: overflow scheduler, CC dropped, jitter, memoire
- WebSocket pour updates temps reel
- Reset etat modal a la fermeture
- Detection type instrument a l'edition
- Verification reponse API avant fermeture des modals

### Tests
- Script `tests/test_api.sh`: 31 tests curl (CRUD, auth, validation, cleanup)

## Limites restantes
- Pas de pipeline CI/CD (compilation firmware non validee automatiquement).
- Les templates avances restent generiques (pas de calibration hardware automatique).
- Le renderer Mappings UI est informatif, pas encore un graphe complet editable.

## Priorites suivantes recommandees
1. Configurer pipeline CI (compilation PlatformIO + execution tests).
2. Ajouter calibration automatique des templates servo.
3. Implementer editeur graphique de pipeline/mappings.
4. Ajouter OTA (Over-The-Air) pour mise a jour firmware sans fil.
