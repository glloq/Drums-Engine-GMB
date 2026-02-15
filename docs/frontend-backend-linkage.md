# Contrôle Frontend ↔ Backend (Phase 2/3)

Ce document vérifie le raccord UI ↔ API pour le modèle action/CC.

## Matrice de liaison
| Fonction UI | Appel frontend | Endpoint backend |
|---|---|---|
| Chargement actionneurs | `api('actuators')` | `GET /api/actuators` |
| CRUD actionneur | `api('actuators'...)`, `api('actuator?id=...')` | `POST/PUT/DELETE /api/actuators?` |
| Test actionneur | `api('test/actuator', 'POST', ...)` | `POST /api/test/actuator` |
| Chargement instruments | `api('instruments')` | `GET /api/instruments` |
| CRUD instrument | `api('instruments'...)`, `api('instrument?id=...')` | `POST/PUT/DELETE /api/instruments?` |
| Test instrument complet | `api('test/instrument', 'POST', ...)` | `POST /api/test/instrument` |
| Test action instrument | `api('instruments/test-action', 'POST', ...)` | `POST /api/instruments/test-action` |
| Templates (liste) | `api('templates')` | `GET /api/templates` |
| Création depuis template | `api('instruments/from-template', 'POST', ...)` | `POST /api/instruments/from-template` |
| Routage CC compilé | `api('cc-routes')` | `GET /api/cc-routes` |
| Capacités UI (types/bus) | `api('capabilities')` | `GET /api/capabilities` |
| Notes MIDI | `api('midi-notes')` | `GET /api/midi-notes` |
| Statut système | `api('status')` | `GET /api/status` |
| Scan I2C | `api('scan-i2c')` | `GET /api/scan-i2c` |
| Sauvegarde | `api('save', 'POST')` | `POST /api/save` |

## Points validés
- Les templates frontend couvrent maintenant aussi `motor_optical` (fallback local + route backend).
- L'éditeur instrument expose `retriggerMode`, `noteOnActions`, `noteOffActions`, `ccBindings`.
- La création depuis template consomme `actuatorSlots` et `slotHints` (GET `/api/templates`) et envoie `actuatorIds[]` vers POST `/api/instruments/from-template`; le backend valide aussi les `slotHints` critiques par template.
- Le backend sérialise/désérialise ces champs et recompille les lookups après mutation.
- L'onglet Mappings affiche un résumé des routes CC compilées.

## Limites connues
- Le rendu Mappings reste simplifié pour la partie pipeline/action graph.
- Le test d'action (`/api/instruments/test-action`) teste une action unitaire (index), pas une macro multi-action complète.

- Le frontend utilise `capabilities.actuatorTypes` + `capabilities.actuatorBehaviors` pour afficher des contraintes de slot lisibles lors de la création depuis template.

- Le frontend applique une pré-validation locale des `slotHints` (actuator type/behavior) avant `POST /api/instruments/from-template`.
