# Roadmap technique (actualisee)

## Phase 1 — Modele actions multi-sorties
**Statut: implementee**
- [x] `ActionStep`, `CCBinding`, `ValueSource`, `RetriggerMode`
- [x] Execution NoteOn/NoteOff via `scheduleActionSteps()`
- [x] Fallback legacy via `actuatorIds`
- [x] Validation serveur de base des payloads actions/CC (bornes, enums, counts)

## Phase 2 — Routage CC temps reel
**Statut: implementee**
- [x] Table compilee de routes CC
- [x] Dispatch runtime via lookup + anti-flood
- [x] Endpoint debug `/api/cc-routes`
- [x] Metriques flood/dispatch dans `/api/status`

## Phase 3 — UI editeur instruments
**Statut: implementee**
- [x] Edition NoteOn/NoteOff/CC bindings
- [x] Retrigger mode et templates
- [x] Test action unitaire (`/api/instruments/test-action`)
- [x] Presets rapides, aide contextuelle, validation locale
- [~] UX avancee (drag/drop ordre, duplication)

## Phase 4 — API/Storage V2
**Statut: implementee**
- [x] CRUD instruments incluant actions/CC/retrigger
- [x] Templates API (`/api/templates`, `/api/instruments/from-template`)
- [x] Recompilation lookup apres mutations instruments
- [x] Versioning format config (v2 avec wrapper objet, retrocompatible v1)

## Phase 5 — Comportements specialises
**Statut: implementee**
- [x] Servo `PITCH_BEND` (mapping centre)
- [x] Servo oscillant `COMB_BRUSH`/`SERVO_STRIKE`
- [x] Templates avances valides (double_hit, shaker, brush, timpani, motor_optical)
- [x] Motor optical dedie (ISR hardware multi-instance, 4 slots)
- [x] Hi-hat controller (`HIHAT_CONTROLLER`: controle CC#4 + splash 80ms)
- [x] Servo mute toggle (position mute/libre)

## Phase 6 — Production readiness
**Statut: implementee (11/12 actions)**
- [x] WiFiManager: credentials `/wifi.json` + mode AP fallback
- [x] ApiAuth: Bearer token + rate-limiting 30 req/s/IP
- [x] ErrorLog: logging persistant `/error.log` 8KB rotatif + `/api/logs`
- [x] Alarmes UI: overflow scheduler, CC dropped, jitter, memoire
- [x] Refactoring web_server en 5 modules
- [x] Tests API: `tests/test_api.sh` (31 tests curl)
- [x] Guide deploiement: `docs/deployment-guide.md`
- [ ] Pipeline CI/CD (compilation PlatformIO automatisee)

## Phase 7 — Fiabilite et concurrence (sprint du 2026-02-18)
**Statut: implementee (18 fixes)**
- [x] Spinlock `_noteActive[]` pour acces dual-core (`portENTER_CRITICAL`)
- [x] Flag ISR atomique (`std::atomic<bool>` pour `schedulerTimerFired`)
- [x] Mutex I2C global + wrapping MCP23017/PCA9685
- [x] Recovery I2C automatique (re-init apres 10 erreurs)
- [x] Persistence loops LittleFS (create/update/delete)
- [x] Ecriture atomique `.tmp` + `rename` (anti-corruption crash)
- [x] Cache `_activeCount` O(1) dans ActuatorManager
- [x] WiFi STA non-bloquant (`vTaskDelay`)
- [x] Tick fractionnaire loop engine (anti-drift timing)
- [x] Guard division/0 dans quantize
- [x] Clamp quantize aux limites de boucle
- [x] Correction index loop apres delete
- [x] Reset etat modal instrument a la fermeture
- [x] Detection type instrument a l'edition (au lieu de forcer custom)
- [x] Verification reponse API avant fermeture modals
- [x] Pitch bend CC#126 (evite collision CC#127)
- [x] STACK retrigger compteur uint8_t (au lieu de bool)
- [x] NoteOff velocity passthrough

## Risques / dette technique
- Couverture tests: script curl fonctionnel mais pas de CI automatise.
- Dependance environnement outillage (`pio`) pour validation complete firmware.
- Pipeline editor UI non implemente (vision long terme).
