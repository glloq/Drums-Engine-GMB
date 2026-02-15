# Arduino IDE - Guide rapide

## Compilation avec Arduino IDE 2.x

### 1. Configuration de la carte

**Obligatoire** - Verifiez ces parametres dans le menu Outils :

| Parametre | Valeur requise |
|-----------|----------------|
| Board | **ESP32 Dev Module** |
| Flash Size | **4MB** |
| Partition Scheme | **Default 4MB with spiffs** |
| Upload Speed | **921600** |
| Core Debug Level | None |
| Erase All Flash | Only Before Upload (pour reset complet) |

### 2. Bibliotheques requises

Installer via Library Manager (Croquis > Inclure une bibliotheque > Gerer) :

- **ArduinoJson** >= 7.0.0
- **Adafruit MCP23017** >= 2.3.0
- **Adafruit PWM Servo Driver** >= 3.0.0
- **AppleMIDI** >= 3.2.0
- **MIDI Library** >= 5.0.2

**Installation manuelle requise** (non disponibles dans Library Manager) :
- **ESPAsyncWebServer** >= 1.2.3 : https://github.com/me-no-dev/ESPAsyncWebServer
- **AsyncTCP** >= 1.1.1 : https://github.com/me-no-dev/AsyncTCP

**Incluses avec ESP32 Arduino Core** (aucune installation requise) :
- **DNSServer** - Pour le portail captif WiFi
- **WiFi** - Gestion WiFi STA/AP
- **ESPmDNS** - Service mDNS (.local)
- **LittleFS** - Systeme de fichiers

Telecharger les ZIP depuis GitHub et installer via :
Croquis > Inclure une bibliotheque > Ajouter une bibliotheque .ZIP

### 3. Compilation

1. Ouvrir **engine.ino** (ce repertoire)
2. Verifier/Compiler : **Ctrl+R** ou Croquis > Verifier/Compiler
3. Telecharger : **Ctrl+U** ou Croquis > Telecharger

### 4. Uploader le filesystem (UI web)

1. Installer le plugin **ESP32 LittleFS Data Upload** :
   https://github.com/lorol/arduino-esp32littlefs-plugin
2. Placer les fichiers UI web dans le dossier `data/`
3. Outils > **ESP32 Sketch Data Upload**

### 5. Moniteur serie

- Outils > Moniteur serie
- Vitesse : **115200 baud**
- Le token API et l'adresse IP s'affichent au boot

---

## Erreurs frequentes

### "bootloader.bin était inattendu"

**Solution 1** : Verifier le schema de partition
- Outils > Partition Scheme > **"Default 4MB with spiffs"**

**Solution 2** : Nettoyer le cache de compilation
- Fermer Arduino IDE
- Supprimer les dossiers temporaires :
  - Windows : `%TEMP%\arduino_build_*` et `%TEMP%\arduino_cache_*`
  - macOS : `~/Library/Arduino15/tmp/`
  - Linux : `/tmp/arduino_build_*`
- Redemarrer Arduino IDE

**Solution 3** : Chemin de projet trop long (Windows)
- Deplacer le projet vers `C:\Projects\Drums-Engine-MIDI\`

**Solution 4** : Utiliser PlatformIO (plus stable)
```bash
cd engine
pio run -t upload
```

### "Multiple definition of setup/loop"

Le fichier `engine.ino` doit rester **vide**. Les fonctions `setup()` et `loop()` sont definies dans `src/main.cpp`.

### Bibliotheques introuvables

Verifier que toutes les bibliotheques sont installees, y compris ESPAsyncWebServer et AsyncTCP (installation manuelle requise).

---

## Documentation complete

Voir [`docs/deployment-guide.md`](../docs/deployment-guide.md) pour le guide complet.
