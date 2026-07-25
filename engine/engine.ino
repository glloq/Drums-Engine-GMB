// ============================================================================
// Drums Engine - Arduino IDE entry point
// ============================================================================
//
// Ce fichier existe pour la compatibilite Arduino IDE 2.x.
// L'implementation reelle est dans src/main.cpp qui fournit setup() et loop().
//
// Arduino IDE compile automatiquement tous les .cpp dans src/ recursivement,
// donc ce fichier .ino peut rester vide.
//
// === CONFIGURATION ARDUINO IDE ===
//
// 1. Board Manager: installer "esp32 by Espressif Systems" (>= 3.0.0)
//    URL: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
//
// 2. Board: "ESP32 Dev Module"
//    - Flash Size: 4MB
//    - Partition Scheme: "Default 4MB with spiffs" (ou "Default")
//    - Upload Speed: 921600
//
// 3. Bibliotheques requises (installer via Library Manager):
//    - ArduinoJson            >= 7.0.0   (bblanchon)
//    - ESPAsyncWebServer      >= 3.6.0   (esp32async fork, core 3.x)
//    - AsyncTCP               >= 3.3.2   (esp32async fork, core 3.x)
//    - AppleMIDI              >= 3.2.0   (lathoub)
//    - MIDI Library            >= 5.0.2   (lathoub/FortySevenEffects)
//    - Adafruit MCP23017      >= 2.3.0   (Adafruit)
//    - Adafruit PWM Servo     >= 3.0.0   (Adafruit)
//
// 4. Filesystem (LittleFS):
//    - Installer le plugin "ESP32 LittleFS Data Upload" pour Arduino IDE
//    - Placer les fichiers UI web dans engine/data/
//    - Tools > ESP32 Sketch Data Upload pour flasher le filesystem
//
// 5. Compiler: Sketch > Verify/Compile (Ctrl+R)
//    Flasher:  Sketch > Upload (Ctrl+U)
//
// ============================================================================
//
// NOTE: Si la compilation echoue avec "multiple definition of setup/loop",
// verifier que ce fichier .ino ne contient PAS de fonctions setup/loop
// (elles sont definies dans src/main.cpp).
//
// ============================================================================
