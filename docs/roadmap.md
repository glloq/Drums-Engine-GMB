# Roadmap technique (actualisée)

## Phase 1 — Modèle actions multi-sorties
**Statut: majoritairement implémentée**
- [x] `ActionStep`, `CCBinding`, `ValueSource`, `RetriggerMode`
- [x] Exécution NoteOn/NoteOff via `scheduleActionSteps()`
- [x] Fallback legacy via `actuatorIds`
- [x] Validation serveur de base des payloads actions/CC (bornes, enums, counts)

## Phase 2 — Routage CC temps réel
**Statut: implémentée (MVP)**
- [x] Table compilée de routes CC
- [x] Dispatch runtime via lookup + anti-flood
- [x] Endpoint debug `/api/cc-routes`
- [~] Métriques de flood/dispatch détaillées à ajouter

## Phase 3 — UI éditeur instruments
**Statut: implémentée (MVP)**
- [x] Édition NoteOn/NoteOff/CC bindings
- [x] Retrigger mode et templates
- [x] Test action unitaire (`/api/instruments/test-action`)
- [~] UX avancée (drag/drop ordre, validation inline, duplication)

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
- [x] Motor optical dedie (ISR hardware `attachInterrupt` + `portMUX` spinlock)
- [x] Hi-hat controller (`HIHAT_CONTROLLER`: controle CC#4 + splash 80ms)

## Phase 6 — Production readiness (NOUVEAU)
**Statut: implementee (11/12 actions)**
- [x] WiFiManager: credentials `/wifi.json` + mode AP fallback
- [x] ApiAuth: Bearer token + rate-limiting 30 req/s/IP
- [x] ErrorLog: logging persistant `/error.log` 8KB rotatif + `/api/logs`
- [x] Alarmes UI: overflow scheduler, CC dropped, jitter, memoire
- [x] Refactoring web_server en 5 modules
- [x] Tests API: `tests/test_api.sh` (31 tests curl)
- [x] Guide deploiement: `docs/deployment-guide.md`
- [ ] Pipeline CI/CD (compilation PlatformIO automatisee)

## Risques / dette technique
- Couverture tests: script curl fonctionnel mais pas de CI automatise.
- Dependance environnement outillage (`pio`) pour validation complete firmware.
- Pipeline editor UI non implemente (vision long terme).
