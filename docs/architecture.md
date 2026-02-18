# Architecture technique

## Vue d'ensemble

Le moteur suit une architecture 6 couches descendantes, executee sur ESP32 dual-core FreeRTOS :

```
Core 1 (RT 1kHz)                    Core 0 (App)
┌────────────────────┐              ┌──────────────────┐
│ 1. MIDI Layer      │              │ Web Server REST  │
│ 2. Event Processor │              │ Loop Engine      │
│ 3. Scheduler       │              │ WiFi Manager     │
│ 4. Actuator Mgr    │              │ Storage LittleFS │
│ 5. HAL Drivers     │              └──────────────────┘
└────────────────────┘
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
MIDI → lookup note_to_pipeline[128] → ActionStep[] → scheduler → actuator
```

### CC (Control Change)
```
MIDI CC → update global vars → lookup cc_routes[] → anti-flood 5ms → scheduler → actuator
```

### Pitch Bend
```
MIDI PitchBend → map 14-bit → 0-127 → route via CC#126 virtuel → cc_routes → scheduler
```
> CC#126 est utilise (et non CC#127) pour eviter la collision avec le vrai CC#127 (Poly Mode On).

### Loop Engine
```
LoopEngine::update() → tick fractionnaire → _dispatchEvent() → EventProcessor → pipeline
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

### PipelineLookup
- `note_to_pipeline[128]` — O(1) lookup par note MIDI
- `cc_routes[]`, `cc_to_first[128]`, `cc_to_count[128]` — table CC compilee

## Securite temps reel et concurrence

### Dual-core FreeRTOS
- **Core 1 (RT)** : MIDI + Scheduler + Watchdog (tache pinnee)
- **Core 0 (App)** : Web server + Loop Engine + WiFi

### Protections concurrence
- `portENTER_CRITICAL` spinlock sur `_noteActive[128]` (partage Core 0 / Core 1)
- `std::atomic<bool>` pour le flag ISR timer `schedulerTimerFired`
- Mutex I2C global (`g_i2cMutex`) protegeant tous les acces MCP23017/PCA9685
- `portMUX` par slot ISR pour le comptage edges moteur optique

### Protections I2C
- Mutex FreeRTOS (`xSemaphoreCreateMutex`) avec timeout 5ms
- Compteur d'erreurs I2C par driver (auto-recovery apres 10 erreurs consecutives)
- Re-initialisation automatique du bus Wire + re-init driver

### Anti-flood et surcharge
- Throttle CC : max 1 dispatch batch par numero CC toutes les 5ms
- Protection surcharge : `MAX_CONCURRENT_ACTIVE` actionneurs simultanes
- Watchdog periodique avec timeouts par type d'actionneur
- Cooldown par actionneur (configurable en microsecondes)

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

## Motor optical (ISR hardware)

- 4 slots ISR maximum (`MAX_OPTICAL_MOTORS`)
- Trampolines ISR par slot via macro `MAKE_ISR(N)`
- `portMUX` spinlock par slot pour comptage atomique des edges
- Allocation dynamique de slot au pin du capteur
- Detachement ISR automatique avant commande PWM

## Composants de code

```
engine/src/
├── core/           Config, types, constantes, error_log
├── hal/            Drivers: MCP23017, PCA9685, LEDC, GPIO + interface I2C mutex
├── actuator/       Comportements: servo, solenoid, motor + manager avec cache activeCount
├── scheduler/      Ordonnanceur temps reel (queue statique, timer hardware)
├── event/          Pipeline compile, traitement MIDI runtime, spinlock _noteActive
├── instrument/     Gestion instruments, templates, factory actionneurs
├── midi/           Moteur rtpMIDI (AppleMIDI)
├── loop/           Sequenceur boucles (tick fractionnaire, quantize, persistence)
├── storage/        LittleFS (ecriture atomique, migration v4)
├── wifi/           WiFi STA/AP + portail captif (vTaskDelay non-bloquant)
└── web/            Serveur REST (5 modules) + UI gzip + auth + rate-limiting
```
