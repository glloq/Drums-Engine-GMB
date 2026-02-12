// ============================================================================
// MidiPercussion V4 - ESP32 WROOM
// ============================================================================
// Architecture:
//   - Instruments definis via interface web (plus besoin de modifier le code)
//   - Chaque instrument = N actionneurs avec timing et MIDI mapping
//   - MIDI via WiFi (rtpMIDI/AppleMIDI) au lieu de USB
//   - Serveur web pour configuration, test et sequenceur de boucles
//   - Stockage persistant sur LittleFS (flash)
//
// Types d'actionneurs supportes:
//   - Solenoid Strike : frappe simple on/off (MCP23017)
//   - Solenoid Hold   : solenoide avec maintien PWM (PCA9685)
//   - Servo Position  : position servo (hi-hat, choke)
//   - Servo Strike    : servo oscillant (maracas, shaker)
//   - Comb/Brush      : peigne/brosse plastique (cymbales, snare)
//   - Pitch Bend      : tension de peau pour pitch bend
//
// Hardware:
//   - ESP32 WROOM (WiFi + BT)
//   - MCP23017 (I2C GPIO) pour solenoïdes
//   - PCA9685  (I2C PWM) pour servos
//   - I2C: SDA=21, SCL=22
// ============================================================================

#include <WiFi.h>
#include <ESPmDNS.h>
#include <Wire.h>

#include "config.h"
#include "actuatorTypes.h"
#include "scheduler.h"
#include "actuatorDriver.h"
#include "instrumentManager.h"
#include "midiEngine.h"
#include "loopEngine.h"
#include "storage.h"
#include "webServer.h"

// --- Global instances ---
Scheduler scheduler;
ActuatorDriver actuatorDriver(&scheduler);
InstrumentManager instrumentManager(&actuatorDriver, &scheduler);
MidiEngine midiEngine(&instrumentManager);
LoopEngine loopEngine(&instrumentManager);
Storage storage;
WebServerManager webServer(&instrumentManager, &actuatorDriver, &loopEngine, &midiEngine, &storage);

// --- WiFi connection ---
bool connectWiFi() {
  DBGF("[WiFi] Connecting to '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    DBG(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    DBGLN("");
    DBGF("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

    // mDNS
    if (MDNS.begin(WIFI_HOSTNAME)) {
      MDNS.addService("apple-midi", "udp", RTP_MIDI_PORT);
      MDNS.addService("http", "tcp", WEB_PORT);
      DBGF("[mDNS] %s.local\n", WIFI_HOSTNAME);
    }
    return true;
  }

  DBGLN("\n[WiFi] STA connection failed!");

  if (WIFI_AP_FALLBACK) {
    DBGF("[WiFi] Starting AP: '%s'\n", WIFI_AP_SSID);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    DBGF("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());

    if (MDNS.begin(WIFI_HOSTNAME)) {
      MDNS.addService("http", "tcp", WEB_PORT);
    }
    return true;
  }

  return false;
}

// --- Setup ---
void setup() {
  #if DEBUG_SERIAL
  Serial.begin(DEBUG_BAUD);
  delay(500);
  #endif

  DBGLN("============================================");
  DBGLN("  MidiPercussion V4 - ESP32 WROOM");
  DBGLN("============================================");

  // 1. Storage (LittleFS)
  if (!storage.begin()) {
    DBGLN("[FATAL] Storage init failed!");
  }

  // 2. I2C Hardware
  if (!actuatorDriver.begin()) {
    DBGLN("[WARN] No I2C actuator modules found");
    DBGLN("[WARN] System will work in config-only mode");
  }

  // 3. Load saved instruments
  if (!storage.loadInstruments(instrumentManager)) {
    DBGLN("[INFO] No saved instruments, starting fresh");
  } else {
    DBGF("[INFO] Loaded %d instruments\n", instrumentManager.getInstrumentCount());
  }

  // 4. WiFi
  if (!connectWiFi()) {
    DBGLN("[FATAL] WiFi init failed!");
    // Continue anyway - serial debug still works
  }

  // 5. MIDI over WiFi
  midiEngine.begin();

  // 6. Web Server
  webServer.begin();

  DBGLN("============================================");
  DBGLN("  System Ready!");
  DBGF("  Web UI: http://%s.local\n", WIFI_HOSTNAME);
  DBGF("  MIDI:   %s (port %d)\n", MIDI_SESSION_NAME, RTP_MIDI_PORT);
  DBGF("  Instruments: %d\n", instrumentManager.getInstrumentCount());
  DBGLN("============================================");
}

// --- Main loop ---
void loop() {
  // Process MIDI input
  midiEngine.update();

  // Process scheduled events (deactivation timers, etc.)
  scheduler.update();

  // Process loop playback
  loopEngine.update();

  // WebSocket cleanup
  webServer.update();
}
