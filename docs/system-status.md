# État des lieux actuel (mise à jour)

## Résumé
Le moteur supporte désormais un modèle instrument orienté actions multi-étapes (NoteOn/NoteOff) avec routage CC compilé et éditeur UI dédié.

## Capacités implémentées
- Scheduler temps réel (timer hardware, queue statique, watchdog).
- Exécution action-step:
  - `ActionStep` NoteOn/NoteOff (jusqu'à 4 étapes / événement)
  - `ValueSource`: velocity, fixed, CC var, inverted velocity
  - courbes velocity et delays par étape
- Routage CC Phase 2:
  - table compilée (`cc_routes`, `cc_to_first`, `cc_to_count`)
  - dispatch runtime anti-flood (5ms)
  - endpoint debug `/api/cc-routes`
- API templates:
  - `/api/templates`
  - `/api/instruments/from-template`
- UI instruments Phase 3:
  - éditeurs NoteOn/NoteOff/CC bindings
  - retrigger mode
  - tests action unitaire (`/api/instruments/test-action`)
- Comportements servo avancés (Phase 5 partielle):
  - `PITCH_BEND` (mapping centré)
  - `COMB_BRUSH`/`SERVO_STRIKE` oscillants via watchdog

## Limites restantes
- Pas de build CI/compilation firmware validée dans cet environnement (`pio` absent).
- Les templates avancés restent génériques (pas de calibration hardware automatique).
- Le renderer Mappings UI est informatif, pas encore un graphe complet éditable.
- Cas “motor optical” dédié non implémenté (nouveau type hardware/ISR à faire).

## Priorités suivantes recommandées
1. Ajouter validation serveur détaillée sur payload actions/CC (ranges, IDs existants).
2. Ajouter endpoint de diagnostic temps réel (compteurs anti-flood CC, erreurs scheduler).
3. Implémenter type actuateur motor optical (HAL + feedback + ISR).
4. Ajouter tests d’intégration API (templates, test-action, cc-routes).
