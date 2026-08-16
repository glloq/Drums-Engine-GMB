# Architecture technique

## Vue d'ensemble

Le moteur suit une architecture 6 couches descendantes, executee sur ESP32 dual-core FreeRTOS :

```
Core 1 (RT 1kHz)                    Core 0 (App)
┌────────────────────┐              ┌──────────────────┐
│ 1. MIDI Layer      │              │ Web Server REST  │
│ 2. Event Processor │──LedEvent──→ │ LED Engine 60fps │
│ 3. Scheduler       │  (lock-free) │ Loop Engine      │
│ 4. Actuator Mgr    │              │ WiFi Manager     │
│ 5. HAL Drivers     │              │ Storage LittleFS │
└────────────────────┘              └──────────────────┘
```

## Couches logiques

1. **MIDI Layer** — Reception rtpMIDI (AppleMIDI WiFi, port 5004)
2. **Event Processor** — Lookup compile note→pipeline, evaluation actions, routage CC
3. **Scheduler** — Queue statique 128 commandes horodatees, timer hardware 1 kHz
4. **Actuator Manager** — Dispatch commandes, watchdog, protection surcharge
5. **HAL Drivers** — MCP23017 (GPIO I2C), PCA9685 (PWM I2C), LEDC (PWM local), GPIO direct
6. **Storage** — Persistance LittleFS avec ecriture atomique (.tmp + rename)

## Flux d'execution

### NoteOn/NoteOff
```
MIDI → lookup note_to_pipeline[canal-1][note] → ActionStep[] → scheduler → actuator
```

### CC (Control Change)
```
MIDI CC → update global vars → lookup cc_routes[] → filtre canal → anti-flood 5ms → scheduler
```

### Pitch Bend
```
MIDI PitchBend → map 14-bit → 0-127 → route via CC#126 virtuel → cc_routes → scheduler
```
> CC#126 est utilise (et non CC#127) pour eviter la collision avec le vrai CC#127 (Poly Mode On).

### Aftertouch (Channel Pressure)
```
MIDI Aftertouch → pressure 0-127 → route via CC#125 virtuel → cc_routes → scheduler
```
> CC#125 est reserve pour le routage interne de l'aftertouch. Anti-flood CC standard (5ms).

### Loop Engine
```
LoopEngine::update() → tick fractionnaire → _dispatchEvent() → EventProcessor → pipeline
```

### LED Engine (WS2812B)
```
EventProcessor ──→ LedEventQueue (SPSC lock-free) ──→ LedEngine (Core 0, 60fps)
                                                        ├─ Poll events
                                                        ├─ Hit animations (flash/pulse/ripple)
                                                        ├─ Idle/theme (static/breathing/rainbow)
                                                        ├─ Blend hit + idle → couleur finale
                                                        └─ LedStripDriver → RMT → WS2812B
```

## Structures cles

### InstrumentConfig
- `noteOnActions[]`, `noteOffActions[]` — jusqu'a 4 ActionStep par evenement
- `ccBindings[]` — routage CC vers actionneurs
- `retriggerMode` — IGNORE, RESET, STACK

### ActionStep
- `commandType` — PULSE, POSITION, PWM, OFF
- `valueSource` — VELOCITY, FIXED, CC_VAR, INVERTED
- `velocityCurve` — LINEAR, EXPO, LOG
- `delayMs`, `durationMinMs`, `durationMaxMs`

### CompiledPipeline
- Actions/bindings compilees + `retrigger_mode`
- `alternate_group_count` pour round-robin
- Jusqu'a 8 blocs par pipeline

### Pipeline Blocks (19 types)
| Categorie | Subtype | Description |
|---|---|---|
| **CONDITION** | Threshold | Compare variable/valeur vs seuil |
| | Round-Robin | Alternance cyclique entre N groupes |
| | Velocity Split | Gate si velocity dans [min, max] |
| | Random | Probabilite N% de passage |
| | Note Range | Gate si valeur dans [min, max] |
| **TRANSFORM** | Velocity Curve | Applique courbe (linear/expo/log) |
| | Gain | Multiplie par N% (100=1x) |
| | Clamp | Limite entre min et max |
| | Invert | Inverse (127 - value) |
| | Set Variable | Stocke value dans variable globale |
| **TIME** | Pulse | Velocity → duree impulsion (terminal) |
| | Delay | Retarde sortie de N ms |
| | Ramp | Micro-steps progressifs (terminal) |
| | Gate | Bloque si intervalle < N ms |
| **OUTPUT** | Pulse | Commande PULSE (terminal) |
| | Position | Commande POSITION (terminal) |
| | PWM | Commande PWM (terminal) |
| | Off | Commande OFF (terminal) |

### PipelineLookup
- `note_to_pipeline[16][128]` — lookup O(1) par **(canal, note)**
- `cc_routes[]`, `cc_to_first[128]`, `cc_to_count[128]` — table CC compilee,
  chaque route portant le canal de son instrument

## Routage MIDI : le canal fait partie de la cle

La table de lookup etait indexee par la **note seule**. Le canal n'etait pas une
cle : en mode pipeline il etait purement ignore (une note 36 sur le canal 1
declenchait le kick du canal 10), et en mode legacy il n'etait verifie qu'apres
coup, contre un slot unique par note — deux instruments sur la meme note dans
deux canaux differents ne pouvaient donc pas coexister, le second ecrasait le
premier et le premier ne repondait plus.

La table est desormais `16 x 128` (2 ko) et le canal est une vraie cle. Cela
permet de faire cohabiter plusieurs familles sur un seul ESP32 :

```
CH10  kit batterie          CH12  xylophone
CH11  percussions latines   CH13  glockenspiel
```

### Regles de precedence

Les deux chemins de routage — pipelines compiles et instruments legacy —
partagent la meme implementation (`core/midi_routing.h`, testee dans
`test/test_midi_routing`), pour qu'ils ne puissent pas diverger :

1. `midiChannel == 0` signifie **OMNI** : l'entree est depliee sur les 16 lignes
   a la compilation. Le chemin temps reel reste donc un seul acces indexe, sans
   branche supplementaire.
2. Un canal explicite l'emporte **toujours** sur une entree OMNI reclamant la
   meme note, quel que soit leur ordre dans la configuration (construction en
   deux passes).
3. Entre deux entrees de meme rang, la premiere garde la place ; les suivantes
   sont comptees comme masquees et signalees dans les logs plutot qu'ignorees
   silencieusement.
4. Un canal hors de `[1..16]` en entree est **refuse**, jamais replie sur le
   canal 1.

Le compteur `_noteActive` de l'`EventProcessor` est lui aussi passe d'un index
par note a un index **par pipeline** : une note presente sur deux canaux suit
maintenant son NoteOn/NoteOff independamment. L'API expose toujours un tableau
de 128 notes pour l'UI, reconstruit a la demande.

## Securite temps reel et concurrence

### Dual-core FreeRTOS
- **Core 1 (RT)** : MIDI + Scheduler + Watchdog (tache pinnee)
- **Core 0 (App)** : Web server + Loop Engine + LED Engine + WiFi

### Protections concurrence
- `portENTER_CRITICAL` spinlock sur `_noteActive[]` (partage Core 0 / Core 1)
- `std::atomic<bool>` pour le flag ISR timer `schedulerTimerFired`
- Mutex I2C global (`g_i2cMutex`) protegeant tous les acces MCP23017/PCA9685
- `portMUX` par slot ISR pour le comptage edges moteur optique

### Protections I2C
- Mutex FreeRTOS (`xSemaphoreCreateMutex`) avec timeout 5ms
- Compteur d'erreurs I2C par driver (auto-recovery apres 10 erreurs consecutives)
- Re-initialisation automatique du bus Wire + re-init driver

### Anti-flood et surcharge
- Throttle CC : max 1 dispatch batch par **(canal, CC)** toutes les 5ms
- Protection surcharge : budget electrique (voir ci-dessous)
- Watchdog periodique avec timeouts par type d'actionneur
- Cooldown par actionneur (configurable en microsecondes)

### Budget electrique

La protection de surcharge etait un compteur plat : au plus
`MAX_CONCURRENT_ACTIVE` (8) actionneurs actifs. Cela revient a supposer qu'ils
consomment tous pareil, alors que l'ecart atteint un facteur 100 entre un servo
de hi-hat (~20 mA) et un solenoide de grosse caisse (~2 A). Sur une petite
alimentation la limite etait trop permissive ; sur une grosse installation elle
devenait une limite **musicale** arbitraire.

L'arbitrage porte desormais sur le courant declare par actionneur
(`ActuatorConfig::currentMa`), avec trois plafonds independants — chacun
desactivable en le mettant a 0 :

| Plafond | Effet |
|---|---|
| `maxPeakMa` | Plafond dur : aucune activation ne le franchit, quelle que soit la priorite |
| `maxContinuousMa` | Plafond souple : au-dela, seules les priorites NORMAL/HIGH passent |
| `maxConcurrent` | Plafond en nombre, conserve comme garde-fou independant |

Chaque actionneur porte une priorite (`PRIO_LOW` / `PRIO_NORMAL` / `PRIO_HIGH`) :
au-dessus du courant continu, les ornements sont sacrifies pour garder la
structure rythmique.

**Migration transparente** : un actionneur dont `currentMa` vaut 0 (courant non
declare) ne consomme pas de budget et n'est jamais refuse pour un motif de
courant. Les defauts compiles laissent `maxPeakMa` et `maxContinuousMa` a 0 et
`maxConcurrent` a 8 : une configuration existante se comporte donc exactement
comme avant tant que rien n'est declare.

La regle est une fonction pure (`core/power_budget.h`), testee dans
`test/test_power_budget`. L'etat et les refus comptabilises par motif sont
exposes sur `GET /api/power-budget`.

### Queue scheduler
- Ring buffer statique 128 commandes
- Zero allocation dynamique dans le chemin critique
- Horodatage microseconde

## Persistance

### LittleFS
- Ecriture atomique : `.tmp` + `rename()` pour eviter la corruption en cas de crash
- Format versionne v2 : `{"version":2, "actuators":[...]}` (retrocompatible v1)
- Loops persistees automatiquement sur create/update/delete
- Migration transparente depuis format V4 legacy

### Fichiers de configuration
```
/actuators.json     — Configuration des actionneurs
/instruments.json   — Configuration des instruments
/leds.json          — Configuration LED (strips, segments, brightness)
/power.json         — Budget electrique (plafonds crete/continu/nombre)
/themes.json        — Themes LED (palettes, animations)
/wifi.json          — Credentials WiFi
/auth.json          — Token API
/error.log          — Logs persistants (8KB max, rotation auto)
/loops/loop_N.json  — Boucles individuelles
```

## Comportements actionneurs

| Behavior | Type | Description |
|---|---|---|
| SOLENOID_STRIKE | Solenoid | Frappe impulsionnelle (pulse calibre) |
| SOLENOID_HOLD | Solenoid | Maintien tant que note active |
| SERVO_POSITION | Servo | Position proportionnelle a la velocity |
| SERVO_STRIKE | Servo | Frappe oscillante via watchdog |
| COMB_BRUSH | Servo | Balayage oscillant type brosse |
| PITCH_BEND | Servo | Mapping centre pour pitch bend |
| HIHAT_CONTROLLER | Servo | CC#4 continu + splash 80ms sur PULSE |
| SERVO_MUTE | Servo | Etouffoir toggle (position mute/libre) |
| SOLENOID_MUTE | Solenoid | Etouffoir solenoid |
| MOTOR_OPTICAL_TRACK | Motor | Suivi optique ISR hardware (4 slots) |
| STEPPER_POSITION | Stepper | Position absolue en pas (zone de frappe, tension) |
| STEPPER_STRIKE | Stepper | Aller-retour maillet : frappe puis retour au repos |
| STEPPER_ROTATE | Stepper | Rotation continue a vitesse variable |

## Stepper (cinquieme type d'actionneur)

Un stepper n'est indispensable a aucune batterie classique. Il devient
interessant des que la **repetabilite de la position** compte plus que la vitesse
de frappe : maillet rotatif, mecanisme de roulement, changement de zone de
frappe, reglage de tension de peau (timbale), pedale mecanique.

### Cablage (bus `GPIO_DIRECT` obligatoire)

| Champ | Role |
|---|---|
| `hwPin` | STEP |
| `hwAddress` | DIR |
| `enablePin` | /ENABLE actif bas (255 = driver toujours actif) |
| `endStopPin1` | Butee mini / origine |
| `endStopPin2` | Butee maxi |
| `paramMin`/`paramMax` | Bornes de course en pas (en % de vitesse pour ROTATE) |
| `paramDefault` | Position de repos |
| `stepperMaxSps` | Vitesse max en pas/seconde |
| `stepperAccelSps2` | Acceleration en pas/s² (0 = pas de rampe) |

Ni MCP23017 (un aller-retour I2C par pas) ni PCA9685 (50 Hz) ne peuvent produire
un train d'impulsions ; LEDC n'aide pas non plus puisque les pas doivent etre
comptes. Le validateur impose donc `GPIO_DIRECT`.

### Generation des pas

Les impulsions sortent de `Actuator::service()`, appele a chaque passe de la
boucle temps reel (~1 kHz) via `ActuatorManager::serviceAll()`. Une passe peut
emettre une **rafale bornee** de `STEPPER_MAX_STEPS_PER_PASS` pas, ce qui porte
le plafond a ~8 kpas/s tout en bornant la charge : 8 pas x (4 µs haut + 4 µs bas)
= ~64 µs par passe dans le pire cas, soit ~6 % d'une passe de 1 ms. C'est le
compromis assume — pas de timer materiel dedie, mais une charge deterministe et
un plafond de vitesse annonce (`STEPPER_MAX_SPEED_SPS`, impose par le validateur).

La vitesse suit une rampe d'acceleration, avec deceleration d'approche calculee
sur la distance restante (`v = sqrt(2·a·d)`) pour que le moteur arrive a l'arret
plutot qu'en pleine vitesse — sinon la charge continue sur son inertie et des pas
sont perdus.

### Prise d'origine

Si une butee mini est declaree, la position reste **inconnue** au demarrage et le
premier deplacement absolu declenche une prise d'origine (approche lente, bornee
par `STEPPER_HOMING_TIMEOUT_US`) avant de rejoindre la cible demandee. Sans
butee, la position de repos declaree est prise pour argent comptant. Un timeout
de prise d'origine laisse la position marquee inconnue, pour qu'un deplacement
ulterieur retente au lieu de partir d'un zero invente.

## Motor optical (ISR hardware)

- 4 slots ISR maximum (`MAX_OPTICAL_MOTORS`)
- Trampolines ISR par slot via macro `MAKE_ISR(N)`
- `portMUX` spinlock par slot pour comptage atomique des edges
- Allocation dynamique de slot au pin du capteur
- Detachement ISR automatique avant commande PWM

## Composants de code

```
engine/src/
├── core/           Config, types, constantes, error_log,
│                   midi_routing (regles (canal,note)), power_budget (arbitrage)
├── hal/            Drivers: MCP23017, PCA9685, LEDC, GPIO + interface I2C mutex
├── actuator/       Comportements: servo, solenoid, motor, stepper + manager
│                   (budget electrique, pool type-efface)
├── scheduler/      Ordonnanceur temps reel (queue statique, timer hardware)
├── event/          Pipeline compile, traitement MIDI runtime, spinlock _noteActive
├── instrument/     Gestion instruments, templates, factory actionneurs
├── led/            LED WS2812B: strip driver (RMT), event queue, engine 60fps, themes
├── midi/           Moteur rtpMIDI (AppleMIDI)
├── loop/           Sequenceur boucles (tick fractionnaire, quantize, persistence)
├── storage/        LittleFS (ecriture atomique, migration v4)
├── wifi/           WiFi STA/AP + portail captif (vTaskDelay non-bloquant)
└── web/            Serveur REST (6 modules) + UI gzip + auth + rate-limiting
```

## Systeme LED (WS2812B)

### Architecture
- **4 strips max** via 4 canaux RMT ESP32 (GPIO 4, 5, 16, 17 par defaut)
- **16 segments max** : portion de strip assignee a un instrument
- **8 themes max** : palettes couleurs + modes animation globaux
- **Communication** : SPSC lock-free ring buffer (LedEventQueue) Core 1 → Core 0

### Pipeline de rendu (60 fps sur Core 0)
1. Poll `LedEventQueue` : events NoteOn/NoteOff du Core 1
2. Mise a jour animations hit actives (fade out progressif)
3. Calcul couleurs idle/theme par segment
4. Blend hit (prioritaire) + idle → couleur finale
5. Ecriture pixels via `LedStripDriver` → RMT hardware → WS2812B

### Animations
- **Hit** : FLASH (instantane + fade), PULSE (montee/descente), RIPPLE (propagation centre→bords)
- **Idle** : OFF, STATIC (fixe), BREATHING (sinusoide 3s), RAINBOW (arc-en-ciel defilant)
- Le hit a priorite sur l'idle via alpha blending

### Hardware
- Level shifter SN74HCT125 recommande (3.3V ESP32 → 5V WS2812B)
- Alimentation 5V externe pour les strips (ne pas alimenter via le 5V ESP32)
- Condensateur 1000µF sur l'alimentation 5V
