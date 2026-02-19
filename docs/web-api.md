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
- `GET /api/actuators/status` — etat temps reel leger (id + active seulement, pour polling)
- `POST /api/actuators`
- `PUT /api/actuator?id=<id>`
- `DELETE /api/actuator?id=<id>`

### Tests
- `POST /api/test/instrument`
- `POST /api/test/actuator`
- `POST /api/test/note` — envoyer une note MIDI brute (piano virtuel)
- `POST /api/test/cc` — envoyer un CC MIDI

### Notes actives (temps reel)
- `GET /api/notes/active` — retourne les notes MIDI actuellement actives

### Loops
- `GET /api/loops`
- `GET /api/loop?id=<id>`
- `POST /api/loops`
- `PUT /api/loop?id=<id>`
- `DELETE /api/loop?id=<id>`
- `POST /api/loop/play?id=<id>`
- `POST /api/loop/stop`

### LED Strips
- `GET /api/led/strips` — liste des strips configures
- `POST /api/led/strips` — configurer un strip
- `DELETE /api/led/strip?id=<id>` — supprimer un strip
- `POST /api/led/strip/test` — tester un strip (allume blanc)

### LED Segments
- `GET /api/led/segments` — liste des segments
- `POST /api/led/segments` — creer un segment
- `PUT /api/led/segment?id=<id>` — modifier un segment
- `DELETE /api/led/segment?id=<id>` — supprimer un segment
- `POST /api/led/segment/test` — tester un segment

### LED Themes
- `GET /api/led/themes` — liste des themes + theme actif
- `POST /api/led/themes` — creer un theme
- `PUT /api/led/theme?id=<id>` — modifier un theme
- `DELETE /api/led/theme?id=<id>` — supprimer un theme
- `POST /api/led/theme/active` — activer un theme

### LED Status
- `GET /api/led/status` — status LED (brightness, counts, overflow)
- `POST /api/led/brightness` — regler la luminosite master

### Pipeline Editor
- `GET /api/pipeline/block-schema` — schema complet des types de blocs (pour UI dynamique)
- `GET /api/pipeline/blocks?instrumentId=<id>` — blocs pipeline + actions d'un instrument
- `PUT /api/pipeline/blocks?instrumentId=<id>` — mettre a jour le pipeline (blocs + actions + CC)
- `POST /api/pipeline/test` — tester un pipeline (simule NoteOn)

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

### MIDI Configuration
- `GET /api/midi/channels` — canaux MIDI actifs (bitmask + array)
- `PUT /api/midi/channels` — configurer les canaux (`{channelMask: 0xFFFF}` ou `{channels: [1,10]}`)

### Securite
- `GET /api/auth-token` — obtenir le token API (acces local uniquement)

### Logs
- `GET /api/logs` — logs recents (ring buffer) + optionnel `?full=1` pour le fichier complet
- `DELETE /api/logs` — vider les logs

### Maintenance
- `POST /api/factory-reset` — supprime toute la config et redemarre l'ESP32 (auth requise)

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
- Messages emis par le serveur:
  - `{"type":"midi_note","note":<0-127>,"on":true/false}` — changement d'état d'une note MIDI (polling ~30ms)

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


## Pipeline Editor API

### `GET /api/pipeline/block-schema`
Retourne le schema complet des types de blocs disponibles, avec leurs sous-types, parametres et descriptions.
Utilise par l'UI pour generer dynamiquement les formulaires de configuration des blocs.

Reponse (extrait):
```json
[
  {
    "type": "condition",
    "typeId": 0,
    "label": "Condition",
    "color": "#e94560",
    "subtypes": [
      {
        "subtype": 0, "id": "threshold", "label": "Seuil",
        "params": [
          {"name": "param1", "label": "Variable", "type": "uint8", "default": 127, "min": 0, "max": 255},
          {"name": "param2", "label": "Seuil", "type": "uint16", "default": 64, "min": 0, "max": 127}
        ]
      },
      {"subtype": 3, "id": "random", "label": "Aleatoire", "params": [...]},
      ...
    ]
  },
  ...
]
```

### `GET /api/pipeline/blocks?instrumentId=<id>`
Retourne le pipeline compile d'un instrument: blocs, actions NoteOn/NoteOff, CC bindings.

### `PUT /api/pipeline/blocks?instrumentId=<id>`
Met a jour le pipeline d'un instrument, recompile le lookup, et sauvegarde.

```json
{
  "blocks": [
    {"type": "condition", "subtype": 2, "param1": 40, "param2": 127},
    {"type": "transform", "subtype": 1, "param1": 0, "param2": 150}
  ],
  "outputActuatorId": 3,
  "retriggerMode": 0,
  "noteOnActions": [...],
  "noteOffActions": [...],
  "ccBindings": [...]
}
```

### `POST /api/pipeline/test`
Simule un NoteOn pour tester le pipeline d'un instrument.

```json
{"instrumentId": 3, "velocity": 80}
```

### Types de blocs pipeline (19 sous-types)

| Categorie | ID | Sous-type | Description |
|---|---|---|---|
| condition | 0 | threshold | Compare variable/valeur a un seuil |
| condition | 1 | round_robin | Alternance entre N groupes |
| condition | 2 | velocity_split | Gate velocity [min, max] |
| condition | 3 | random | Probabilite (0-100%) |
| condition | 4 | note_range | Gate valeur [min, max] |
| transform | 0 | velocity_curve | Courbe (linear/expo/log) |
| transform | 1 | gain | Gain en % |
| transform | 2 | clamp | Clamp [min, max] |
| transform | 3 | invert | 127 - value |
| transform | 4 | set_var | Stocke dans variable globale |
| time | 0 | pulse | Velocity → duree (terminal) |
| time | 1 | delay | Retarde de N ms |
| time | 2 | ramp | N micro-steps (terminal) |
| time | 3 | gate | Min N ms entre events |
| output | 0 | pulse | Commande PULSE (terminal) |
| output | 1 | position | Commande POSITION (terminal) |
| output | 2 | pwm | Commande PWM (terminal) |
| output | 3 | off | Commande OFF (terminal) |

### CC virtuels internes
| CC# | Usage |
|---|---|
| 125 | Aftertouch (channel pressure) |
| 126 | Pitch Bend (14-bit → 7-bit) |
