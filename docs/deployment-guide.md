# Guide de deploiement — Drums-Engine-MIDI

Ce guide couvre l'installation, la compilation, le flash et la mise en service du moteur de percussion MIDI sur ESP32.

---

## 1. Prerequis materiels

### Carte microcontroleur
- **ESP32 DevKit** (board `esp32dev` dans PlatformIO)
- Connexion USB pour alimentation + programmation

### Modules I2C
- **MCP23017** : extenseur GPIO (sorties digitales pour solenoides)
  - Adresse de base : `0x20` (jusqu'a 4 modules : `0x20`-`0x23`)
- **PCA9685** : controleur PWM 16 canaux (servos, moteurs)
  - Adresse de base : `0x40` (jusqu'a 4 modules : `0x40`-`0x43`)
  - Frequence PWM : 50 Hz

### Bus I2C
- **SDA** : GPIO 21
- **SCL** : GPIO 22
- **Frequence** : 400 kHz

### Actionneurs supportes
- **Servos** (via PCA9685)
- **Solenoides** (via MCP23017 ou GPIO direct)
- **Moteurs DC** (via LEDC PWM ou GPIO direct)
- **Motor Optical** (moteur + capteur optique, GPIO direct)

### Alimentation
- **ESP32** : alimentation via USB (programmation et debug)
- **Actionneurs** : alimentation externe separee (5V-12V selon actionneurs)
- Ne pas alimenter les actionneurs directement depuis l'ESP32

---

## 2. Installation de l'environnement

### Option A — PlatformIO (recommandee)

PlatformIO gere automatiquement les dependances et la compilation.

**Extension VSCode :**
1. Installer [Visual Studio Code](https://code.visualstudio.com/)
2. Installer l'extension "PlatformIO IDE" depuis le marketplace
3. Redemarrer VSCode

**CLI :**
```bash
pip install platformio
```

### Option B — Arduino IDE 2.x

L'Arduino IDE est une alternative plus accessible. Le fichier `engine/engine.ino` fournit le point d'entree.

**1. Installer Arduino IDE 2.x** depuis [arduino.cc](https://www.arduino.cc/en/software)

**2. Ajouter le support ESP32 :**
- Fichier > Preferences > URLs de gestionnaire de cartes, ajouter :
  ```
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
  ```
- Outils > Board Manager > Installer "esp32 by Espressif Systems" (>= 2.0.0)
- Selectionner la carte : **ESP32 Dev Module**

**3. Installer les bibliotheques** via Library Manager (Croquis > Inclure une bibliotheque > Gerer) :

| Bibliotheque | Version | Auteur |
|---|---|---|
| ArduinoJson | >= 7.0.0 | bblanchon |
| ESPAsyncWebServer | >= 1.2.3 | me-no-dev |
| AsyncTCP | >= 1.1.1 | me-no-dev |
| AppleMIDI | >= 3.2.0 | lathoub |
| MIDI Library | >= 5.0.2 | FortySevenEffects |
| Adafruit MCP23017 | >= 2.3.0 | Adafruit |
| Adafruit PWM Servo Driver | >= 3.0.0 | Adafruit |

> **Note :** ESPAsyncWebServer et AsyncTCP ne sont pas dans le Library Manager officiel. Il faut les installer manuellement en telechargeant les `.zip` depuis GitHub et via Croquis > Inclure une bibliotheque > Ajouter une bibliotheque .ZIP.

**4. Parametres de la carte** (Outils) :

| Parametre | Valeur |
|---|---|
| Board | ESP32 Dev Module |
| Flash Size | 4MB |
| Partition Scheme | Default 4MB with spiffs |
| Upload Speed | 921600 |

**5. Ouvrir le projet :**
- Fichier > Ouvrir > selectionner `engine/engine.ino`
- Arduino IDE compilera automatiquement tous les `.cpp` dans `src/`

**6. Uploader le filesystem (LittleFS) :**
- Installer le plugin [ESP32 LittleFS Data Upload](https://github.com/lorol/arduino-esp32littlefs-plugin)
- Outils > ESP32 Sketch Data Upload

### Cloner le repository

```bash
git clone <url-du-repository>
cd Drums-Engine-MIDI
```

### Structure du projet

```
Drums-Engine-MIDI/
├── engine/                  # Firmware ESP32
│   ├── engine.ino           # Point d'entree Arduino IDE
│   ├── platformio.ini       # Configuration PlatformIO
│   ├── src/                 # Code source firmware
│   │   ├── main.cpp
│   │   ├── core/            # Config, types, constantes
│   │   ├── hal/             # Drivers hardware (I2C, GPIO, LEDC)
│   │   ├── actuator/        # Gestion actionneurs
│   │   ├── scheduler/       # Ordonnanceur temps reel
│   │   ├── event/           # Pipeline compile, traitement MIDI
│   │   ├── instrument/      # Gestion instruments
│   │   ├── midi/            # Moteur rtpMIDI (AppleMIDI)
│   │   ├── loop/            # Moteur de boucles
│   │   ├── storage/         # Persistance LittleFS
│   │   ├── wifi/            # Gestion WiFi (AP/STA)
│   │   └── web/             # Serveur web REST + UI
│   └── data/                # Fichiers statiques (UI web, LittleFS)
└── docs/                    # Documentation
```

---

## 3. Compilation et flash du firmware

### Avec PlatformIO

Toutes les commandes depuis le repertoire `engine/` :

```bash
cd engine

# Compiler
pio run

# Flasher le firmware
pio run -t upload

# Uploader le filesystem (UI web)
pio run -t uploadfs

# Moniteur serie
pio device monitor
```

PlatformIO telecharge automatiquement les dependances. Vitesse upload : **921600 baud**.

### Avec Arduino IDE

1. Ouvrir `engine/engine.ino` dans Arduino IDE 2.x
2. **Compiler** : Croquis > Verifier/Compiler (Ctrl+R)
3. **Flasher** : Croquis > Telecharger (Ctrl+U)
4. **Filesystem** : Outils > ESP32 Sketch Data Upload (plugin LittleFS requis)
5. **Moniteur serie** : Outils > Moniteur serie (**115200 baud**)

> Le fichier `.ino` est vide — `setup()` et `loop()` sont fournis par `src/main.cpp`. Arduino IDE compile automatiquement tous les `.cpp` dans `src/`.

### Sortie attendue au boot

Le moniteur serie affiche les logs de demarrage, l'adresse IP, le token API, et les diagnostics runtime.

---

## 4. Configuration WiFi initiale

### Premier demarrage (mode AP)

Au premier demarrage (ou si aucun fichier `/wifi.json` n'existe sur LittleFS), le moteur demarre automatiquement en **mode Access Point** :

| Parametre        | Valeur             |
|------------------|--------------------|
| SSID             | `MidiPerc-Setup`   |
| Mot de passe     | `midiperc`          |
| IP du portail    | `192.168.4.1`      |

### Connexion au portail

1. Connecter un ordinateur ou telephone au reseau WiFi `MidiPerc-Setup`
2. Ouvrir un navigateur a l'adresse : **http://192.168.4.1**
3. L'UI web de configuration s'affiche

### Configurer le WiFi via l'API

Envoyer une requete POST pour configurer le reseau WiFi cible :

```bash
curl -X POST http://192.168.4.1/api/wifi \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <token>" \
  -d '{"ssid":"VotreReseau","password":"VotreMotDePasse"}'
```

Parametres optionnels :
- `"hostname"` : nom mDNS (defaut : `midipercussion`)

### Redemarrage automatique

Apres sauvegarde des credentials, l'ESP32 **redemarre automatiquement** et tente de se connecter au reseau WiFi configure (mode STA).

- Les credentials sont persistes dans `/wifi.json` sur LittleFS
- Si la connexion echoue (reseau indisponible, mauvais mot de passe), l'ESP32 retombe automatiquement en mode AP

### Verifier l'etat WiFi

```bash
curl http://<ip-esp32>/api/wifi
```

Reponse en mode STA :
```json
{
  "mode": "sta",
  "ssid": "VotreReseau",
  "ip": "192.168.1.42",
  "hostname": "midipercussion",
  "rssi": -45,
  "connected": true
}
```

---

## 5. Securite API

### Token d'authentification

Au premier demarrage, un **token API** (16 caracteres hexadecimaux) est genere automatiquement et persiste dans `/auth.json` sur LittleFS.

### Recuperer le token

**Via le moniteur serie** — Le token est affiche dans la console au boot :
```
  API Token:    a1b2c3d4e5f67890
```

**Via l'API** :
```bash
curl http://<ip-esp32>/api/auth-token
```

Reponse :
```json
{
  "token": "a1b2c3d4e5f67890",
  "usage": "Authorization: Bearer <token>"
}
```

### Utilisation du token

Les requetes de **modification** (POST, PUT, DELETE) necessitent le header d'authentification :

```
Authorization: Bearer <token>
```

Exemple :
```bash
curl -X POST http://<ip-esp32>/api/actuators \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer a1b2c3d4e5f67890" \
  -d '{"name":"Kick Solenoid","type":0,"bus":0,"hwAddress":0,"hwPin":0}'
```

Les requetes **GET** (lecture seule) ne necessitent **pas** d'authentification.

### Rate-limiting

Le serveur web integre un rate-limiter : **30 requetes par seconde par IP**. Au-dela, les requetes recoivent une reponse `429 Too Many Requests`.

---

## 6. Verification post-deploiement

### Acceder a l'interface web

Ouvrir dans un navigateur :
- **http://midipercussion.local** (via mDNS)
- ou **http://\<ip-esp32\>** (adresse IP directe)

### Verifier l'etat du systeme

```bash
curl http://<ip-esp32>/api/status
```

Ce endpoint retourne un snapshot complet : actionneurs actifs, statistiques MIDI, diagnostics CC, etat du scheduler.

### Detecter les modules I2C

```bash
curl http://<ip-esp32>/api/scan-i2c
```

Verifie que les modules MCP23017 et PCA9685 sont detectes sur le bus I2C.

### Tester un actionneur

```bash
curl -X POST http://<ip-esp32>/api/test/actuator \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <token>" \
  -d '{"id":1,"value":80}'
```

Cela active brievement l'actionneur avec l'ID `1` a la valeur `80` (0-127).

### Consulter les logs

```bash
curl http://<ip-esp32>/api/logs
```

Retourne les entrees recentes du ring buffer de logs (erreurs et evenements systeme).

Pour effacer les logs :
```bash
curl -X DELETE http://<ip-esp32>/api/logs \
  -H "Authorization: Bearer <token>"
```

---

## 7. Configuration MIDI

### rtpMIDI (AppleMIDI sur WiFi)

| Parametre         | Valeur              |
|-------------------|---------------------|
| Port rtpMIDI      | `5004`              |
| Session mDNS      | `MidiPercussion`    |
| Canal MIDI defaut | `10` (GM Percussion)|

### Connexion depuis macOS

1. Ouvrir **Audio MIDI Setup** (`/Applications/Utilities/`)
2. Afficher la fenetre MIDI (menu Fenetre > Afficher le studio MIDI)
3. Double-cliquer sur **Reseau**
4. La session `MidiPercussion` devrait apparaitre automatiquement dans le repertoire
5. Selectionner la session et cliquer **Connecter**

### Connexion depuis Windows

1. Installer [rtpMIDI by Tobias Erichsen](https://www.tobias-erichsen.de/software/rtpmidi.html)
2. Creer une nouvelle session sur le port `5004`
3. Dans le repertoire, la session `MidiPercussion` devrait apparaitre via Bonjour/mDNS
4. Connecter la session

### Canal MIDI

Le canal par defaut est le **canal 10** (convention General MIDI pour les percussions). Les instruments peuvent etre configures individuellement sur d'autres canaux via l'API ou l'UI web.

---

## 8. Depannage

### Moniteur serie

Le moniteur serie est l'outil principal de diagnostic :

```bash
# PlatformIO
cd engine && pio device monitor

# Arduino IDE : Outils > Moniteur serie
```

Vitesse : **115200 baud**. Affiche les messages de demarrage, les erreurs, et l'activite MIDI en temps reel.

### Pas de connexion WiFi

- L'ESP32 **retombe automatiquement en mode AP** (`MidiPerc-Setup`) si la connexion au reseau WiFi echoue
- Verifier le SSID et le mot de passe via `GET /api/wifi` en mode AP
- Reconfigurer avec `POST /api/wifi`

### Pas de modules I2C detectes

- Verifier le cablage : SDA (GPIO 21), SCL (GPIO 22)
- Verifier l'alimentation des modules
- Utiliser `GET /api/scan-i2c` pour lister les adresses detectees
- Le systeme **fonctionne sans modules I2C** en mode configuration seule (API et UI web operationnelles, mais pas d'actionneurs I2C)

### Erreurs persistantes

Consulter le log complet via l'API :

```bash
curl http://<ip-esp32>/api/logs
```

Les logs utilisent un ring buffer en memoire. Les entrees les plus anciennes sont ecrasees automatiquement.

### Pas d'activite MIDI

- Verifier que l'ESP32 est connecte au meme reseau que la source MIDI
- Verifier la session rtpMIDI dans le logiciel source (port 5004)
- Consulter `GET /api/status` pour les compteurs `notesReceived` et `notesSent`
- Le mode AP ne supporte pas rtpMIDI (le WiFi AP est reserve a la configuration)

### Reset complet

Pour effacer toute la configuration (WiFi, actionneurs, instruments, token) et repartir de zero :

**PlatformIO :**
```bash
cd engine && pio run -t erase
pio run -t upload && pio run -t uploadfs
```

**Arduino IDE :**
- Outils > Erase All Flash Before Sketch Upload > Enabled
- Re-telecharger le firmware puis le filesystem

### LED de statut

Le firmware actuel **n'implemente pas de LED de statut**. Si necessaire, une LED peut etre ajoutee sur un GPIO libre pour indiquer l'etat WiFi ou l'activite MIDI.
