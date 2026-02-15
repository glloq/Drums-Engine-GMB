# API Web embarquée

Base URL: `http://<ip-esp32>/`

## Endpoints principaux

### Instruments
- `GET /api/instruments`
- `GET /api/instrument?id=<id>`
- `POST /api/instruments`
- `PUT /api/instrument?id=<id>`
- `DELETE /api/instrument?id=<id>`
- `POST /api/instruments/from-template`
- `POST /api/instruments/test-action`

### Actuators
- `GET /api/actuators`
- `POST /api/actuators`
- `PUT /api/actuator?id=<id>`
- `DELETE /api/actuator?id=<id>`

### Tests
- `POST /api/test/instrument`
- `POST /api/test/actuator`

### Loops
- `GET /api/loops`
- `GET /api/loop?id=<id>`
- `POST /api/loops`
- `PUT /api/loop?id=<id>`
- `DELETE /api/loop?id=<id>`
- `POST /api/loop/play?id=<id>`
- `POST /api/loop/stop`

### Système / debug
- `GET /api/status`
- `GET /api/scan-i2c`
- `GET /api/midi-notes`
- `GET /api/capabilities`
- `GET /api/templates`
- `GET /api/cc-routes`
- `POST /api/save`

## Payloads utiles

### `POST /api/instruments/from-template`
```json
{
  "template": "kick",
  "name": "Kick 1",
  "midiNote": 36,
  "midiChannel": 10,
  "actuatorIds": [3]
}
```

Les templates utilisent désormais des slots d'actuateurs explicites (`actuatorIds`, ou `primaryActuatorId`/`secondaryActuatorId`).

Templates backend actuels:
- `kick`
- `hihat`
- `cymbal_mute`
- `double_hit`
- `shaker`
- `brush`
- `timpani`
- `motor_optical`

### `POST /api/instruments/test-action`
Permet de tester une action spécifique sans jouer la note complète.

```json
{
  "id": 3,
  "event": "on",
  "index": 0,
  "velocity": 90
}
```

- `event`: `"on"` ou `"off"`
- `index`: index de l'action dans `noteOnActions`/`noteOffActions`

### `GET /api/cc-routes`
Retourne la table compilée de routage CC active. Le champ `dropped` indique les routes ignorées quand `MAX_CC_ROUTES` est dépassé.

```json
{
  "count": 3,
  "dropped": 1,
  "routes": [
    {
      "cc": 4,
      "entries": [
        {
          "actuatorId": 7,
          "commandType": 1,
          "rangeMin": 30,
          "rangeMax": 150,
          "curve": 0,
          "inverted": false
        }
      ]
    }
  ]
}
```

### `GET /api/status`
Expose un snapshot runtime, incluant diagnostics CC:
- `cc_route_count`: routes CC compilées actives
- `cc_route_dropped`: routes compilées mais ignorées (capacité table)
- `cc_rx_total`: événements CC reçus
- `cc_throttled_total`: événements CC ignorés par anti-flood (5ms)
- `cc_routed_batches_total`: batches CC routés
- `cc_routed_commands_total`: commandes actuateur émises depuis les CC
- `cc_unrouted_total`: événements CC sans route compilée

## WebSocket
- Endpoint: `/ws`
- Usage: diffusion d'état et supervision temps réel côté UI.

## Notes d'implémentation
- Les handlers renvoient JSON.
- La validation JSON est faite côté firmware avec `ArduinoJson`.
- Les opérations CRUD instruments recompilent le lookup pipeline/CC et persistent dans LittleFS.


## Validation instrument renforcée
- Les `actuatorId` des `noteOnActions`, `noteOffActions` et `ccBindings` doivent exister dans la configuration actuateurs.
- `velocityCurve` et `ccBindings.curve` doivent être dans `[CURVE_LINEAR..CURVE_LOG]`.
- Les doublons CC (`ccNumber + actuatorId + commandType`) sont rejetés.
- Les plages inversées sont normalisées automatiquement (`min/max` pour durées et range CC).


## Notes moteur optical
- Type actuateur `MOTOR_OPTICAL` exposé dans `GET /api/capabilities`.
- Validation backend: bus actuel obligatoire `GPIO_DIRECT` (lecture capteur optique).
- Validation backend: `behavior` obligatoire `MOTOR_OPTICAL_TRACK`.
- Pour ce type, `hwPin` = sortie moteur, `hwAddress` = pin capteur optique (entrée).


### Exemple motor_optical
```json
{
  "template": "motor_optical",
  "name": "Roto Tom Motor",
  "midiNote": 60,
  "actuatorIds": [12]
}
```
> Le type d'actuateur cible doit être configuré en `MOTOR_OPTICAL` + `MOTOR_OPTICAL_TRACK`.


## Métadonnées templates
- `GET /api/templates` inclut `slotHints[]` pour préciser les contraintes par slot (ex: `requiredType`, `requiredBehavior`).
- Le frontend peut utiliser ces hints pour guider la sélection des `actuatorIds`.


## Compatibilité action/actuateur
- Validation backend rejette les `ActionStep`/`CCBinding` avec `commandType` incompatible avec le type d'actuateur ciblé.


## Capacités enrichies
- `GET /api/capabilities` expose désormais `actuatorBehaviors[]` (id + name) en plus de `actuatorTypes[]`.

- Création depuis template `motor_optical`: validation stricte de slot (type `MOTOR_OPTICAL` + behavior `MOTOR_OPTICAL_TRACK`).


## Validation stricte des slots template
- `POST /api/instruments/from-template` valide désormais les slots critiques: `kick`, `hihat`, `cymbal_mute`, `double_hit`, `shaker`, `brush`, `timpani`, `motor_optical`.
- En cas de mismatch type/comportement de l'actuateur, la requête est rejetée avant création.

- Pour `shaker`/`brush`, la validation de slot impose actuellement un actuateur de type `PWM_MOTOR`.

- L'UI effectue une pré-validation des `slotHints` (type/behavior) avant appel API pour retour utilisateur immédiat.
