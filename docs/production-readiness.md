# Audit projet — Livrable Production

**Date**: 2026-02-15
**Version auditee**: branche `main` (commit 828615c)

---

## 1. Etat global du projet

| Domaine | Avancement | Statut |
|---|---|---|
| Architecture 6 couches | 100% | DONE |
| HAL (GPIO, LEDC, MCP23017, PCA9685) | 100% | DONE |
| Actuators (Servo, Solenoid, Motor) | 90% | Motor optical ISR manquant |
| Scheduler temps reel | 100% | DONE |
| Event Processor + Pipeline | 100% | DONE |
| MIDI Engine (rtpMIDI WiFi) | 100% | DONE |
| Instrument Manager + CRUD | 100% | DONE |
| Loop Engine | 100% | DONE |
| Storage LittleFS | 100% | DONE |
| Web Server REST API | 95% | Rate-limiting manquant |
| UI Web embarquee | 85% | Pipeline editor manquant |
| Templates (8 presets) | 90% | Pas de calibration auto |
| Routage CC compile | 100% | DONE |
| Documentation | 80% | Guides deploy/troubleshoot manquants |
| Tests automatises | 5% | CRITIQUE — quasi absent |
| CI/CD | 0% | CRITIQUE — aucun pipeline |
| Securite | 20% | CRITIQUE — pas d'auth, creds hardcodes |

**Avancement global estime: ~75%**

---

## 2. Ce qui fonctionne (acquis valides)

### 2.1 Coeur temps reel (SOLIDE)
- Timer hardware 1 kHz avec queue statique lock-free (128 commandes)
- Precision microseconde, suivi du jitter
- Watchdog periodique avec limites thermiques/duree par actuateur
- Separation FreeRTOS: Core 1 (RT: MIDI + Scheduler) / Core 0 (Web + Loops)
- Zero allocation dynamique dans le chemin temps reel

### 2.2 Modele instrument (COMPLET)
- ActionStep multi-sorties (4 etapes/evenement) NoteOn/NoteOff
- ValueSource: velocity, fixed, CC variable, inverted velocity
- Courbes velocity: lineaire, exponentielle, logarithmique
- Modes retrigger: IGNORE, RESET, STACK
- Routage CC compile O(1) avec anti-flood 5ms

### 2.3 API REST (FONCTIONNELLE)
- CRUD complet: actuateurs, instruments, boucles
- Templates avec slotHints et validation stricte
- Endpoint capabilities, diagnostics, scan I2C
- Test unitaire d'action, test instrument, test actuateur
- Sauvegarde/chargement LittleFS + migration V4

### 2.4 UI Web embarquee (FONCTIONNELLE)
- SPA complete avec editeurs actuateurs, instruments, boucles
- Presets rapides, aide contextuelle, validation locale
- WebSocket pour updates temps reel
- Reference MIDI, scan I2C integre

---

## 3. Actions BLOQUANTES pour livrable production

### PRIORITE CRITIQUE (P0) — Bloquant livrable

#### 3.1 Securite: Externaliser les credentials WiFi
- **Fichier**: `engine/src/core/config.h:15-19`
- **Probleme**: SSID/password WiFi hardcodes dans le code source
- **Impact**: Impossible de deployer en production sans recompiler
- **Action**: Implementer un portail de configuration WiFi (mode AP + page setup) ou charger les credentials depuis LittleFS (`/wifi.json`)
- **Effort**: Moyen

#### 3.2 Securite: Ajouter authentification API
- **Fichier**: `engine/src/web/web_server.cpp`
- **Probleme**: Aucune authentification sur les endpoints REST
- **Impact**: N'importe qui sur le reseau peut modifier/supprimer la configuration
- **Action**: Implementer au minimum une authentification basique (token ou basic auth) sur les endpoints de modification (POST/PUT/DELETE)
- **Effort**: Moyen

#### 3.3 Validation build firmware
- **Fichier**: `engine/platformio.ini`
- **Probleme**: Aucune validation de compilation dans l'environnement actuel (PlatformIO absent)
- **Impact**: Risque de regression non detectee, pas de binaire valide
- **Action**: Configurer un environnement PlatformIO, valider la compilation complete, generer le binaire `.bin`
- **Effort**: Faible

#### 3.4 Tests de non-regression API
- **Probleme**: Aucun test automatise
- **Impact**: Chaque modification risque de casser l'existant sans detection
- **Action minimum**: Creer une suite de tests HTTP (curl/pytest/Postman) couvrant:
  - CRUD actuateurs (creation, lecture, modification, suppression)
  - CRUD instruments (avec actions, CC bindings)
  - Creation depuis template
  - Validation des payloads invalides (type/bus incompatible, ranges)
  - Endpoints diagnostics (status, cc-routes)
- **Effort**: Moyen

---

### PRIORITE HAUTE (P1) — Important pour fiabilite production

#### 3.5 Rate-limiting API
- **Fichier**: `engine/src/web/web_server.cpp`
- **Probleme**: Pas de limitation de debit sur les endpoints
- **Impact**: Un client defaillant peut saturer le serveur web et perturber le temps reel
- **Action**: Ajouter un rate-limiter simple (max requetes/sec par IP ou global)
- **Effort**: Faible

#### 3.6 Logging persistant des erreurs
- **Probleme**: Les erreurs sont uniquement envoyees sur Serial, perdues si non branche
- **Impact**: Impossible de diagnostiquer les problemes en production
- **Action**: Logger les erreurs critiques dans un fichier LittleFS rotatif (`/error.log`) et les exposer via `/api/logs`
- **Effort**: Moyen

#### 3.7 Finaliser Motor Optical ISR
- **Fichier**: `engine/src/actuator/motor_actuator.cpp`
- **Probleme**: Le suivi capteur optique fonctionne en polling watchdog, pas en interruption hardware
- **Impact**: Precision limitee pour le comptage tours/frappeur moteur
- **Action**: Implementer le mode ISR avec `attachInterrupt()` sur le pin capteur
- **Effort**: Moyen

#### 3.8 Guide de deploiement
- **Probleme**: Aucun guide pour flasher, configurer le WiFi, preparer le hardware
- **Impact**: Deploiement impossible sans connaissance interne du projet
- **Action**: Ecrire un `docs/deployment-guide.md` couvrant:
  - Prerequis materiel (ESP32, I2C, actionneurs)
  - Installation PlatformIO
  - Compilation et flash firmware
  - Upload du filesystem (UI web)
  - Configuration WiFi initiale
  - Verification via scan I2C et test actuateur
- **Effort**: Faible

---

### PRIORITE MOYENNE (P2) — Ameliorations qualite

#### 3.9 Alarmes UI sur erreurs runtime
- **Probleme**: `cc_route_dropped > 0` et overflow scheduler non signales dans l'UI
- **Action**: Ajouter indicateurs visuels dans le dashboard status

#### 3.10 Migration/versioning explicite du format config
- **Probleme**: Pas de numero de version dans les fichiers JSON de config
- **Action**: Ajouter un champ `version` et un mecanisme de migration automatique

#### 3.11 Hi-hat expert behaviors
- **Probleme**: Splashes et conditions multiples non calibres
- **Action**: Finaliser les comportements hi-hat avances avec calibration

#### 3.12 Refactoring web_server.cpp
- **Probleme**: Fichier monolithique avec tous les handlers
- **Action**: Separer en modules (actuator_routes, instrument_routes, system_routes)

---

## 4. Matrice de risques production

| Risque | Probabilite | Impact | Mitigation |
|---|---|---|---|
| Acces non autorise API | Haute | Critique | P0: Ajouter auth |
| Regression apres modif code | Haute | Haute | P0: Tests automatises |
| Binaire firmware non valide | Moyenne | Critique | P0: Valider build |
| Saturation serveur web | Moyenne | Haute | P1: Rate-limiting |
| Perte diagnostic production | Haute | Moyenne | P1: Logging persistant |
| Imprecision moteur optical | Faible | Moyenne | P1: ISR hardware |
| Deploiement rate | Moyenne | Haute | P1: Guide deploiement |
| Config WiFi non modifiable | Haute | Haute | P0: Portail config |

---

## 5. Plan d'action ordonne pour livrable 100%

```
SPRINT 1 — Socle securite & build (BLOQUANT)
├── [P0] 3.1 Externaliser credentials WiFi
├── [P0] 3.2 Ajouter authentification API minimum
├── [P0] 3.3 Valider compilation firmware + generer binaire
└── [P0] 3.4 Suite de tests API de base

SPRINT 2 — Fiabilite production
├── [P1] 3.5 Rate-limiting API
├── [P1] 3.6 Logging persistant erreurs
├── [P1] 3.7 Finaliser motor optical ISR
└── [P1] 3.8 Guide de deploiement complet

SPRINT 3 — Qualite & polish
├── [P2] 3.9 Alarmes UI runtime
├── [P2] 3.10 Versioning format config
├── [P2] 3.11 Hi-hat expert behaviors
└── [P2] 3.12 Refactoring web_server modulaire
```

---

## 6. Resume executif

Le projet **Drums-Engine-MIDI** dispose d'une architecture solide et d'un coeur temps reel fonctionnel. Les couches MIDI, Scheduler, Actuator et Event sont operationnelles. L'API REST et l'UI web couvrent les cas d'usage principaux.

**Pour un livrable production, 4 actions sont bloquantes:**
1. **Securiser** les credentials et l'acces API
2. **Valider** la compilation firmware
3. **Tester** automatiquement les endpoints critiques
4. **Documenter** le deploiement

Le projet est estime a **75% d'avancement**. Les sprints 1 et 2 couvrent le chemin critique vers un livrable production fiable. Le sprint 3 porte sur la qualite et la maintenabilite a long terme.
