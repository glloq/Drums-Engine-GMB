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

### PlatformIO

PlatformIO est requis pour compiler et flasher le firmware. Deux options :

**Option A — Extension VSCode (recommandee) :**
1. Installer [Visual Studio Code](https://code.visualstudio.com/)
2. Installer l'extension "PlatformIO IDE" depuis le marketplace
3. Redemarrer VSCode

**Option B — CLI :**
```bash
pip install platformio
```

### Cloner le repository

```bash
git clone <url-du-repository>
cd Drums-Engine-MIDI
```

### Structure du projet

```
Drums-Engine-MIDI/
├── engine/                  # Firmware ESP32
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

Toutes les commandes suivantes sont a executer depuis le repertoire `engine/` :

```bash
cd engine
```

### Compiler le firmware

```bash
pio run
```

Cela telecharge automatiquement les dependances (ArduinoJson, ESPAsyncWebServer, AppleMIDI, etc.) et compile le firmware.

### Flasher le firmware sur l'ESP32

```bash
pio run -t upload
```

- Vitesse d'upload : **921600 baud**
- Table de partitions : `default.csv`
- Assurez-vous que l'ESP32 est connecte en USB et que le port serie est detecte

### Uploader le filesystem (UI web)

L'interface web est stockee sur la partition LittleFS de l'ESP32 :

```bash
pio run -t uploadfs
```

Cette commande uploade le contenu du dossier `data/` (fichiers HTML/CSS/JS de l'UI web) sur la partition LittleFS.

### Moniteur serie

```bash
pio device monitor
```

- Vitesse : **115200 baud**
- Affiche les logs de demarrage, l'adresse IP, le token API, et les diagnostics runtime

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
cd engine && pio device monitor
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

```bash
cd engine && pio run -t erase
```

Cela efface l'integralite de la memoire flash. Il faudra ensuite re-flasher le firmware et le filesystem :

```bash
pio run -t upload && pio run -t uploadfs
```

### LED de statut

Le firmware actuel **n'implemente pas de LED de statut**. Si necessaire, une LED peut etre ajoutee sur un GPIO libre pour indiquer l'etat WiFi ou l'activite MIDI.
