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
- `POST /api/test/note` — envoyer une note MIDI brute (piano virtuel).
  `{note, velocity, channel, noteOff}` ; `channel` doit etre dans `[1..16]`
  (defaut 10) — le routage refuse un canal hors plage au lieu de le replier sur
  le canal 1, donc la route repond 400 plutot que d'accepter sans rien faire.
- `POST /api/test/cc` — envoyer un CC MIDI. `{cc, value, channel}` ; `channel`
  accepte `[1..16]` (defaut 10). Le routage CC etant lie au canal, un CC de test
  force sur le canal 10 ne pouvait pas exercer une liaison appartenant a un
  instrument d'un autre canal.

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
- `POST /api/loop/pause` — mettre en pause la boucle en cours
- `POST /api/loop/resume` — reprendre la lecture apres pause
- `POST /api/loop/chain` — enchainer plusieurs boucles sequentiellement

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
- `GET /api/capabilities` — types/behaviors d'actionneurs, limites de compilation,
  courbes de velocite, plus un objet `hardware` (broches I2C, LED de statut,
  bouton BOOT, micro I2S, adresses de base MCP/PCA, limites de securite) repris
  de `config.h` et consomme par la page Cablage — voir [`wiring-page.md`](wiring-page.md)
- `GET /api/templates`
- `GET /api/cc-routes`
- `POST /api/save`
- `GET /api/validate` — verification automatique de la configuration (issues, warnings, ok)

### Arret d'urgence
- `POST /api/panic` — arret d'urgence (auth requise). Arrete immediatement tous
  les actionneurs via `ActuatorManager::stopAll()`, vide la queue du scheduler,
  stoppe les boucles et remet a zero l'etat NoteOn/retrigger. Ne passe **pas**
  par la queue normale : reste efficace meme si la queue est saturee. A utiliser
  a la place de commandes `test/actuator value=0` (qui ne sont pas un OFF fiable).

### WiFi
- `GET /api/wifi` — statut WiFi (mode, ssid, ip, rssi)
- `POST /api/wifi` — configurer credentials WiFi (redemarrage auto). **Auth
  requise** + rate-limiting (P0 #2) : la modification des identifiants et le
  redemarrage qu'elle declenche ne sont plus accessibles sans le token API.

### MIDI Configuration
- `GET /api/midi/channels` — canaux MIDI actifs (bitmask + array)
- `PUT /api/midi/channels` — configurer les canaux (`{channelMask: 0xFFFF}` ou `{channels: [1,10]}`)

### Budget electrique
- `GET /api/power-budget` — plafonds configures, consommation instantanee, pire
  cas, et refus comptabilises par motif
- `PUT /api/power-budget` — `{maxPeakMa, maxContinuousMa, maxConcurrent}`
  (persiste dans `/power.json`)

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

`GET /api/capabilities` est **généré depuis la table de descripteurs**
(`engine/src/actuator/actuator_descriptor.cpp`) que lit aussi le validateur. Les
deux ne peuvent donc plus diverger — ce qu'elles faisaient sur les cinq types :
`STEPPER` était absent, `PCA9685` était annoncé pour les moteurs alors que le
validateur le refuse, et `LEDC_PWM` manquait pour les solénoïdes alors que
`SOLENOID_HOLD` l'exige (un solénoïde à maintien était donc impossible à créer
depuis une UI pilotée par l'API).

- `actuatorTypes[]` : `id`, `name`, `recommendedBus`, `supportedBusMask`,
  `supportedBehaviors[]`. Le masque d'un type est l'**union** de ceux de ses
  comportements.
- `actuatorBehaviors[]` : `id`, `name`, `type`, `supportedBusMask`. Le masque est
  par **comportement** : `SOLENOID_STRIKE` accepte MCP23017, `SOLENOID_HOLD`
  exige un vrai PWM. Une UI doit restreindre la liste des bus une fois le
  comportement choisi.
- `buses[]` : `id` + `name` de chaque `HardwareBus`.
- Limites du firmware : `maxPipelines`, `maxCcRoutes`, `midiChannelCount`,
  `maxActionsPerEvent`, `maxCcBindingsPerInstrument`.
- `maxServicedActuators` / `servicedActuatorsUsed` : les actionneurs à mouvement
  continu (steppers) occupent un pool plus petit que `maxActuators`. Un
  dépassement est **refusé** (507), plus seulement journalisé.
- Bloc `hardware` : `maxConcurrentActive`, `powerBudgetPeakMa` et
  `powerBudgetContinuousMa` reflètent le budget **en vigueur** (configurable à
  chaud), plus les constantes compilées ; `stepperMaxSps` donne le plafond de
  vitesse pas-à-pas.

## Champs actionneur ajoutés

`GET /api/actuators`, `POST /api/actuators` et `PUT /api/actuator` acceptent et
renvoient :

| Champ | Défaut | Description |
|---|---:|---|
| `currentMa` | 0 | Courant tiré **pendant l'activation**, en mA. `0` = non déclaré : l'actionneur reste invisible pour le budget électrique et n'est jamais refusé pour un motif de courant. |
| `maxActiveMs` | 0 | Durée d'activation maximale en **millisecondes** (`SOLENOID_HOLD`). `0` = limite de sécurité par défaut du comportement. |
| `priority` | 1 | `0` = Low (ornement, sacrifié en premier), `1` = Normal, `2` = High (structure rythmique). |
| `priorityName` | — | Lecture seule, libellé de la priorité. |

Pour `type = 4` (`STEPPER`) uniquement :

| Champ | Défaut | Description |
|---|---:|---|
| `enablePin` | 255 | GPIO `/ENABLE` actif bas ; `255` = driver toujours actif |
| `stepperMaxSps` | 1000 | Vitesse max en pas/seconde (plafonnée par `stepperMaxSps` des capacités) |
| `stepperAccelSps2` | 0 | Accélération en pas/s² ; `0` = pas de rampe |

Pour un stepper, `hwPin` porte STEP et `hwAddress` porte DIR (et non une adresse
I2C) ; le bus doit être `GPIO_DIRECT`.

**Rétrocompatibilité** : un `actuators.json` antérieur ne contient aucun de ces
champs. Les valeurs par défaut ci-dessus reproduisent exactement le comportement
précédent.

## `GET /api/power-budget`

```json
{
  "maxPeakMa": 15000,
  "maxContinuousMa": 8000,
  "maxConcurrent": 8,
  "activeCurrentMa": 2400,
  "activeCount": 3,
  "worstCaseCurrentMa": 21500,
  "worstCaseExceedsPeak": true,
  "refused": { "byCount": 0, "byPeak": 12, "byContinuous": 3 },
  "priorities": [ {"id":0,"name":"Low"}, {"id":1,"name":"Normal"}, {"id":2,"name":"High"} ]
}
```

- `worstCaseCurrentMa` : somme des courants déclarés si **tous** les actionneurs
  activés se déclenchaient ensemble. `worstCaseExceedsPeak` signale un
  sous-dimensionnement d'alimentation : l'arbitrage fera son travail (des frappes
  seront refusées), ce qui est un avertissement de dimensionnement, pas une
  erreur.
- `refused` : compteurs cumulés depuis le démarrage, par motif de refus.

`PUT` rejette (400) un `maxContinuousMa` supérieur à `maxPeakMa` — il ne
déclasserait jamais rien — et un `maxConcurrent` supérieur à `MAX_ACTUATORS`.
Mettre un plafond à `0` le désactive.

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


## `maxActiveMs` remplace la surcharge de `cooldownUs`

`cooldownUs` portait deux significations : le délai anti-rebond entre deux
activations (en µs/100) **et**, pour `SOLENOID_HOLD`, la durée d'activation
maximale multipliée par 10. Sur un `uint16_t`, cette seconde lecture plafonnait à
6,553 s ; au-delà la valeur repliait, si bien qu'une durée de 60 s demandée par
l'UI devenait ~1 s. Le repli allait toujours dans le sens court — la bobine
coupait plus tôt que demandé, donc côté sûr — mais le réglage ne faisait pas ce
qu'il annonçait.

`cooldownUs` ne signifie plus que l'anti-rebond. La durée maximale a son propre
champ 32 bits, `maxActiveMs`, dans l'unité que l'utilisateur saisit.

**Migration** : `actuators.json` passe en version 3. Un fichier v1/v2 sans
`maxActiveMs` est converti à la lecture (`maxActiveMs = cooldownUs / 10` pour un
`SOLENOID_HOLD`, `cooldownUs` remis à 0). La valeur reprise est celle qui était
**réellement appliquée**, repliement compris : on restaure le comportement
observé, pas une intention supposée. L'API accepte encore l'ancien encodage
envoyé seul, pour qu'une UI en cache continue de fonctionner.

## Ordonnancement transactionnel des événements

`POST /api/test/*` et le chemin MIDI passent par `scheduleActionSteps()`, qui met
en file **toutes** les commandes d'un événement ou aucune. Une percussion peut
déclencher jusqu'à `maxActionsPerEvent` actions ; auparavant elles étaient
insérées une par une, si bien qu'une file presque pleine pouvait accepter la
frappe et refuser l'étouffoir. Un événement refusé ne marque plus la note active
et ne consomme plus de tour de round-robin.

`GET /api/status` expose `event_rejected_batches` et
`scheduler_rejected_batches` pour surveiller le phénomène (à distinguer de
`scheduler_overflow`, qui compte des commandes isolées).

## Modèles de percussion

`GET /api/templates` et `POST /api/instruments/from-template` lisent la **même**
table (`engine/src/instrument/percussion_template.cpp`). Trois descriptions
parallèles coexistaient et avaient divergé :

- le hi-hat annonçait un servo `HIHAT_CONTROLLER` — le comportement qui porte la
  logique CC#4 + splash — alors que la création exigeait `SERVO_POSITION` :
  suivre l'exigence publiée provoquait un 400 ;
- la note par défaut annoncée par chaque modèle était ignorée à la création, qui
  partait d'un `36` codé en dur (un hi-hat atterrissait sur la grosse caisse) ;
- la timbale liait sa tension au CC#127 alors que le moteur convertit le Pitch
  Bend vers le CC virtuel 126 : la liaison ne recevait jamais rien.

`slotHints[].requiredBehavior` est désormais **absent** quand le modèle accepte
n'importe quel comportement du type (le shaker se contente d'un moteur PWM).

La création applique aussi le rollback des autres CRUD : un échec d'écriture
renvoie 500 et retire l'instrument, au lieu de répondre 201 pour une
configuration qui disparaîtrait au redémarrage.
