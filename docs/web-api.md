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
  "midiChannel": 10
}
```

Templates backend actuels:
- `kick`
- `hihat`
- `cymbal_mute`
- `double_hit`
- `shaker`
- `brush`
- `timpani`

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
Retourne la table compilée de routage CC active.

```json
{
  "count": 3,
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

## WebSocket
- Endpoint: `/ws`
- Usage: diffusion d'état et supervision temps réel côté UI.

## Notes d'implémentation
- Les handlers renvoient JSON.
- La validation JSON est faite côté firmware avec `ArduinoJson`.
- Les opérations CRUD instruments recompilent le lookup pipeline/CC et persistent dans LittleFS.
