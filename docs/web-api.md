# API Web embarquée

Base URL: `http://<ip-esp32>/`

## Endpoints principaux
### Instruments
- `GET /api/instruments`
- `GET /api/instrument?id=<id>`
- `POST /api/instruments`
- `PUT /api/instrument?id=<id>`
- `DELETE /api/instrument?id=<id>`

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

### Système
- `GET /api/status`
- `GET /api/scan-i2c`
- `GET /api/midi-notes`
- `POST /api/save`

## WebSocket
- Endpoint: `/ws`
- Usage: diffusion d'état et supervision temps réel côté UI.

## Fichiers statiques
- `/` sert `index.html` depuis LittleFS.

## Notes d'implémentation
- Les handlers renvoient JSON.
- La validation JSON est faite côté firmware avec `ArduinoJson`.
- Les opérations CRUD persistantes déclenchent des sauvegardes dans LittleFS.
