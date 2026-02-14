# Contrôle Frontend ↔ Backend (état actuel)

Ce document vérifie que les ajouts UI sont bien raccordés aux endpoints backend réellement exposés.

## Matrice de liaison
| Fonction UI | Appel frontend | Endpoint backend |
|---|---|---|
| Chargement actionneurs | `api('actuators')` | `GET /api/actuators` |
| Création actionneur | `api('actuators', 'POST', data)` | `POST /api/actuators` |
| Mise à jour actionneur | `api('actuator?id=...', 'PUT', data)` | `PUT /api/actuator?id=...` |
| Suppression actionneur | `api('actuator?id=...', 'DELETE')` | `DELETE /api/actuator?id=...` |
| Test actionneur | `api('test/actuator', 'POST', ...)` | `POST /api/test/actuator` |
| Chargement instruments | `api('instruments')` | `GET /api/instruments` |
| Création instrument | `api('instruments', 'POST', data)` | `POST /api/instruments` |
| Mise à jour instrument | `api('instrument?id=...', 'PUT', data)` | `PUT /api/instrument?id=...` |
| Suppression instrument | `api('instrument?id=...', 'DELETE')` | `DELETE /api/instrument?id=...` |
| Test instrument | `api('test/instrument', 'POST', ...)` | `POST /api/test/instrument` |
| Capacités UI (types/bus) | `api('capabilities')` | `GET /api/capabilities` |
| Notes MIDI | `api('midi-notes')` | `GET /api/midi-notes` |
| Statut système | `api('status')` | `GET /api/status` |
| Scan I2C | `api('scan-i2c')` | `GET /api/scan-i2c` |
| Sauvegarde | `api('save', 'POST')` | `POST /api/save` |

## Points validés
- Le sélecteur **Bus Hardware** est maintenant contraint côté UI selon `supportedBusMask` de `/api/capabilities`.
- Le choix d'un bus incompatible est bloqué visuellement (option désactivée) avant soumission.
- La validation client (`saveActuator`) vérifie aussi la compatibilité type↔bus via les capacités backend.
- Le backend conserve sa validation stricte serveur (`_validateActuatorConfig`) comme filet de sécurité final.

## Limites connues
- L’UI dépend de `/api/capabilities`; en cas d’indisponibilité de cet endpoint, un fallback local est utilisé.
- Le renderer de pipeline dans l’onglet Mappings reste une visualisation V1 simplifiée.
