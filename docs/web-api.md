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

### WiFi
- `GET /api/wifi` — statut WiFi (mode, ssid, ip, rssi)
- `POST /api/wifi` — configurer credentials WiFi (redemarrage auto)

### Securite
- `GET /api/auth-token` — obtenir le token API (acces local uniquement)

### Logs
- `GET /api/logs` — logs recents (ring buffer) + optionnel `?full=1` pour le fichier complet
- `DELETE /api/logs` — vider les logs

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

## Authentification et securite

### Bearer Token
Toutes les requetes **POST, PUT, DELETE** necessitent un header `Authorization: Bearer <token>`.
Les requetes **GET** sont exemptes d'authentification.

Le token est un hex de 16 caracteres, genere automatiquement au premier boot et stocke dans `/auth.json` sur LittleFS.
Pour le recuperer: `GET /api/auth-token` ou dans la sortie Serial au boot.

Exemple:
```bash
curl -X POST http://midipercussion.local/api/save \
  -H "Authorization: Bearer abc123def456789a"
```

### Rate-limiting
Chaque IP est limitee a **30 requetes/seconde** sur les endpoints mutants (POST/PUT/DELETE).
Au-dela, le serveur repond `429 Too Many Requests`.

### Codes d'erreur
| Code | Signification |
|------|---------------|
| 200 | Succes |
| 201 | Ressource creee |
| 400 | Payload invalide / validation echouee |
| 401 | Token manquant ou invalide |
| 404 | Endpoint ou ressource inexistant |
| 429 | Rate-limit depasse |
| 500 | Erreur interne |

## Logging persistant

Les erreurs/warnings sont enregistres dans `/error.log` sur LittleFS (max 8KB, rotation auto).
Un ring buffer de 16 entrees recentes est accessible via `/api/logs`.

Reponse `GET /api/logs`:
```json
{
  "entryCount": 5,
  "recent": [
    {"uptimeS": 42, "level": 0, "message": "WiFi init failed"},
    {"uptimeS": 43, "level": 2, "message": "Engine ready"}
  ]
}
```
Niveaux: `0` = ERROR, `1` = WARN, `2` = INFO.

## Configuration WiFi

Les credentials sont charges depuis `/wifi.json` sur LittleFS. Si absent ou connexion echouee, le systeme demarre en mode AP (`MidiPerc-Setup`/`midiperc`).

`POST /api/wifi`:
```json
{"ssid": "MonReseau", "password": "MonMotDePasse", "hostname": "midipercussion"}
```
Le systeme sauvegarde et redemarre automatiquement.

## Notes d'implementation
- Les handlers renvoient JSON.
- La validation JSON est faite cote firmware avec `ArduinoJson`.
- Les operations CRUD instruments recompilent le lookup pipeline/CC et persistent dans LittleFS.


## Validation instrument renforcée
- Les `actuatorId` des `noteOnActions`, `noteOffActions` et `ccBindings` doivent exister dans la configuration actuateurs.
- `velocityCurve` et `ccBindings.curve` doivent être dans `[CURVE_LINEAR..CURVE_LOG]`.
- Les doublons CC (`ccNumber + actuatorId + commandType`) sont rejetés.
- Les plages inversées sont normalisées automatiquement (`min/max` pour durées et range CC).


## Hi-hat controller (HIHAT_CONTROLLER)
- Behavior servo dedie au controle hi-hat par pedale (CC#4).
- Sur commande `POSITION`: mappe CC 0-127 vers l'angle servo (0=ouvert, 127=ferme).
- Sur commande `PULSE`: declenche un "splash" — ouverture rapide pendant 80ms puis retour a la position courante.
- Template `hihat` utilise ce behavior pour le slot servo (slot 1).

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
