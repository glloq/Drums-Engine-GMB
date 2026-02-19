# Roadmap technique (actualisee)

## Phase 1 — Modele actions multi-sorties
**Statut: implementee**
- [x] `ActionStep`, `CCBinding`, `ValueSource`, `RetriggerMode`
- [x] Execution NoteOn/NoteOff via `scheduleActionSteps()`
- [x] Fallback legacy via `actuatorIds`
- [x] Validation serveur de base des payloads actions/CC (bornes, enums, counts)

## Phase 2 — Routage CC temps reel
**Statut: implementee**
- [x] Table compilee de routes CC
- [x] Dispatch runtime via lookup + anti-flood
- [x] Endpoint debug `/api/cc-routes`
- [x] Metriques flood/dispatch dans `/api/status`

## Phase 3 — UI editeur instruments
**Statut: implementee**
- [x] Edition NoteOn/NoteOff/CC bindings
- [x] Retrigger mode et templates
- [x] Test action unitaire (`/api/instruments/test-action`)
- [x] Presets rapides, aide contextuelle, validation locale
- [~] UX avancee (drag/drop ordre, duplication)

## Phase 4 — API/Storage V2
**Statut: implementee**
- [x] CRUD instruments incluant actions/CC/retrigger
- [x] Templates API (`/api/templates`, `/api/instruments/from-template`)
- [x] Recompilation lookup apres mutations instruments
- [x] Versioning format config (v2 avec wrapper objet, retrocompatible v1)

## Phase 5 — Comportements specialises
**Statut: implementee**
- [x] Servo `PITCH_BEND` (mapping centre)
- [x] Servo oscillant `COMB_BRUSH`/`SERVO_STRIKE`
- [x] Templates avances valides (double_hit, shaker, brush, timpani, motor_optical)
- [x] Motor optical dedie (ISR hardware multi-instance, 4 slots)
- [x] Hi-hat controller (`HIHAT_CONTROLLER`: controle CC#4 + splash 80ms)
- [x] Servo mute toggle (position mute/libre)

## Phase 6 — Production readiness
**Statut: implementee (11/12 actions)**
- [x] WiFiManager: credentials `/wifi.json` + mode AP fallback
- [x] ApiAuth: Bearer token + rate-limiting 30 req/s/IP
- [x] ErrorLog: logging persistant `/error.log` 8KB rotatif + `/api/logs`
- [x] Alarmes UI: overflow scheduler, CC dropped, jitter, memoire
- [x] Refactoring web_server en 5 modules
- [x] Tests API: `tests/test_api.sh` (31 tests curl)
- [x] Guide deploiement: `docs/deployment-guide.md`
- [ ] Pipeline CI/CD (compilation PlatformIO automatisee)

## Phase 7 — Fiabilite et concurrence (sprint du 2026-02-18)
**Statut: implementee (18 fixes)**
- [x] Spinlock `_noteActive[]` pour acces dual-core (`portENTER_CRITICAL`)
- [x] Flag ISR atomique (`std::atomic<bool>` pour `schedulerTimerFired`)
- [x] Mutex I2C global + wrapping MCP23017/PCA9685
- [x] Recovery I2C automatique (re-init apres 10 erreurs)
- [x] Persistence loops LittleFS (create/update/delete)
- [x] Ecriture atomique `.tmp` + `rename` (anti-corruption crash)
- [x] Cache `_activeCount` O(1) dans ActuatorManager
- [x] WiFi STA non-bloquant (`vTaskDelay`)
- [x] Tick fractionnaire loop engine (anti-drift timing)
- [x] Guard division/0 dans quantize
- [x] Clamp quantize aux limites de boucle
- [x] Correction index loop apres delete
- [x] Reset etat modal instrument a la fermeture
- [x] Detection type instrument a l'edition (au lieu de forcer custom)
- [x] Verification reponse API avant fermeture modals
- [x] Pitch bend CC#126 (evite collision CC#127)
- [x] STACK retrigger compteur uint8_t (au lieu de bool)
- [x] NoteOff velocity passthrough

## Phase 8 — Systeme LED WS2812B
**Statut: implementee**
- [x] Types LED: `LedStripConfig`, `LedSegmentConfig`, `LedThemeConfig`, `LedEvent`, `RgbColor`
- [x] Driver WS2812B via ESP32 RMT (`LedStripDriver`) — 4 strips max, buffer RGB
- [x] Queue lock-free SPSC (`LedEventQueue`) — communication Core 1 → Core 0
- [x] Moteur LED 60fps (`LedEngine`) — hit animations + idle/theme + alpha blend
- [x] Animations hit: FLASH, PULSE, RIPPLE (declenchees par MIDI velocity)
- [x] Animations idle: OFF, STATIC, BREATHING (sin 3s), RAINBOW (HSV defilant)
- [x] Themes LED: palettes 8 couleurs, modes globaux, activation dynamique
- [x] Integration EventProcessor: push `LedEvent` sur NoteOn (lock-free)
- [x] Lookup note MIDI → segments LED (rebuild depuis InstrumentManager)
- [x] REST API complete: `/api/led/strips`, `/api/led/segments`, `/api/led/themes`, `/api/led/status`
- [x] Persistance LittleFS: `/leds.json` (strips + segments), `/themes.json`
- [x] UI web: onglet LEDs avec CRUD strips/segments/themes, color pickers, test live
- [x] Config: `LED_MAX_STRIPS=4`, `LED_MAX_SEGMENTS=16`, `LED_MAX_THEMES=8`, `LED_FPS=60`
- [x] GPIO par defaut: 4, 5, 16, 17 (SN74HCT125 level shifter recommande)

## Phase 9 — Pipeline complet et editeur visuel
**Statut: implementee**
- [x] Aftertouch (channel pressure) → CC#125 virtuel, routage anti-flood
- [x] Nouveaux blocs CONDITION: Random (probabilite), NoteRange (filtre valeur)
- [x] Nouveaux blocs TRANSFORM: Invert (127-val), SetVar (ecriture variable globale)
- [x] Nouveau bloc TIME: Gate (intervalle minimum entre events)
- [x] Nouveau bloc OUTPUT: Off (commande arret actionneur)
- [x] 19 sous-types de blocs pipeline (5 CONDITION, 5 TRANSFORM, 4 TIME, 4 OUTPUT)
- [x] API schema blocs: `GET /api/pipeline/block-schema` (description dynamique pour UI)
- [x] API pipeline CRUD: `GET/PUT /api/pipeline/blocks?instrumentId=<id>`
- [x] API test pipeline: `POST /api/pipeline/test` (simule NoteOn)
- [x] Pipeline Editor UI: onglet visuel avec blocs, actions, CC, reordonnancement
- [x] Modal ajout bloc: picker par categorie avec description
- [x] Modal edition bloc: formulaire dynamique genere depuis le schema
- [x] CC virtuels documentes: CC#125 (aftertouch), CC#126 (pitch bend)
- [x] Fix bug hideModal → closeModal dans les modals LED

## Phase 10 — Audit, corrections et outils utilisateur
**Statut: implementee**
- [x] Fix COND_NOTE_RANGE: filtre par note MIDI (et non velocity) via parametre `midiNote` dans pipeline
- [x] Poly aftertouch (MIDI_EVT_POLY_AFTERTOUCH) route via CC#125 comme channel aftertouch
- [x] SOLENOID_MUTE: toggle PULSE, POSITION explicite (< 64 = mute, >= 64 = libre), pas de timeout safety
- [x] API `POST /api/test/note` pour piano virtuel (NoteOn/NoteOff brut)
- [x] Page Piano Virtuel: pads GM percussion (27-81) + mode full (0-127), velocity slider, test CC inline
- [x] Page Calibration: test individuel actionneurs (slider + tester/stop), test instruments (velocity)
- [x] Tab Pipeline renomme (Mappings → Pipeline)
- [x] Block schema COND_NOTE_RANGE: description corrigee ("note MIDI source")

## Phase 11 — Temps reel et UX
**Statut: implementee**
- [x] API `GET /api/notes/active` — notes MIDI actuellement actives (snapshot thread-safe)
- [x] WebSocket broadcast des changements de notes MIDI (~30ms polling)
- [x] Piano Virtuel: mode temps reel avec indicateurs visuels des notes actives (flash + glow)
- [x] Settings modal: grid systeme (firmware, chip, CPU, RAM, flash, uptime, stockage)
- [x] Status API enrichi: chip_model, cpu_freq_mhz, flash_size, min_free_heap, sdk_version, firmware_version
- [x] Tabs scrollables pour mobile (overflow-x auto, 6 onglets)

## Phase 12 — Outils et productivite
**Statut: implementee**
- [x] MIDI Monitor: log temps reel des notes via WebSocket dans les Reglages
- [x] Journal systeme: lecture/effacement des logs moteur depuis l'UI
- [x] Import/Export configuration: sauvegarde JSON complte (actionneurs + instruments)
- [x] MIDI RX indicator: flash jaune dans l'en-tete a chaque note recue
- [x] Emergency STOP ALL: arret immediat de tous les actionneurs (calibration)
- [x] Duplication: cloner un actionneur ou un instrument en un clic
- [x] Filtres de recherche: champs texte pour filtrer actionneurs et instruments
- [x] Help contextuel: aide par type d'actionneur dans le formulaire de creation
- [x] Validation formulaires: nom requis, bornes min/max, avertissement note MIDI dupliquee
- [x] Piano clavier: raccourcis clavier (A-P, 1-8) pour jouer les notes
- [x] Guide de demarrage: instructions pour les nouveaux utilisateurs
- [x] Fix TIME_RAMP: protection division par zero quand la duree est 0

## Phase 13 — Editeur boucles, routage MIDI et calibration avancee
**Statut: implementee**
- [x] Loop editor: separateurs visuels mesures/temps (bordures accent, fonds alternes)
- [x] Loop editor: visualisation velocity (opacite proportionnelle, tooltip)
- [x] Loop editor: slider velocity pour nouvelles notes + clic droit cycle (40/70/100/127)
- [x] Loop editor: playhead temps reel (polling status 100ms, colonne surlignee vert)
- [x] Loop editor: bouton Effacer, compteur notes, grille max 64 pas
- [x] MIDI channel routing: bitmask 16 canaux configurable (defaut: tous)
- [x] MIDI channel routing: filtrage dans tous les callbacks (NoteOn/Off, CC, PitchBend, Aftertouch)
- [x] MIDI channel routing: API `GET/PUT /api/midi/channels`, persistance `/midi.json`
- [x] MIDI channel routing: grille 16 canaux dans Reglages + presets (Tous, Ch.10 Drums)
- [x] Calibration: sweep velocity progressif (20→40→60→80→100→120→127)
- [x] Calibration: affichage des actionneurs lies a chaque instrument
- [x] LED segments: apercu couleurs temps reel dans l'editeur (hit/idle/luminosite)
- [x] Status API enrichi: `loop_tick`, `loop_index`, `midi_channel_mask`
- [x] MIDI Learn: assigner une note en jouant sur le controleur MIDI (via WebSocket)
- [x] Carte MIDI Note Map: vue d'ensemble des 55 notes GM avec assignation couleur
- [x] Affichage "utilise par" sur chaque actionneur (instrument reverse-lookup)
- [x] Actions groupees instruments: activer/desactiver/tester tous
- [x] Reordonnancement actions NoteOn/NoteOff (fleches haut/bas)
- [x] Mobile responsive: taille tactile, modals scroll, header compact
- [x] Derniere note MIDI recue dans l'en-tete (indicateur 2s)
- [x] Storage generic: `saveJsonFile`/`loadJsonFile` pour configs simples
- [x] Calibration: polling temps reel etat actionneurs (badge ACTIF + compteur, 500ms)
- [x] API legere `/api/actuators/status` pour polling rapide (id + active seulement)
- [x] Instruments: slider velocity inline + test direct depuis la liste
- [x] Categorie couleur instruments: drums (rouge), cymbals (or), latin (vert), other (bleu)

## Risques / dette technique
- Couverture tests: script curl fonctionnel mais pas de CI automatise.
- Dependance environnement outillage (`pio`) pour validation complete firmware.
- LED: FastLED non utilise (RMT natif via espShow), a evaluer si besoin de plus d'animations.
