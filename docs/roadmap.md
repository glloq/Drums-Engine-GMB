# Roadmap technique (actualisée)

## Phase 1 — Modèle actions multi-sorties
**Statut: majoritairement implémentée**
- [x] `ActionStep`, `CCBinding`, `ValueSource`, `RetriggerMode`
- [x] Exécution NoteOn/NoteOff via `scheduleActionSteps()`
- [x] Fallback legacy via `actuatorIds`
- [~] Validation stricte des payloads actions à compléter

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
**Statut: partiellement implémentée**
- [x] CRUD instruments incluant actions/CC/retrigger
- [x] Templates API (`/api/templates`, `/api/instruments/from-template`)
- [x] Recompilation lookup après mutations instruments
- [ ] Migration/versioning explicite de format config à formaliser

## Phase 5 — Comportements spécialisés
**Statut: partiellement implémentée**
- [x] Servo `PITCH_BEND` (mapping centré)
- [x] Servo oscillant `COMB_BRUSH`/`SERVO_STRIKE`
- [~] Templates avancés (double_hit, shaker, brush, timpani)
- [ ] Motor optical dédié (nouveau type + HAL + ISR)
- [ ] Hi-hat expert (splashes/conditions multiples calibrées)

## Risques / dette technique
- Très gros diffs historiques dans certains fichiers critiques (web_server/index) → prioriser refactor modulaire.
- Couverture tests automatisés encore faible.
- Dépendance environnement outillage (`pio`) pour validation complète firmware.
