// ============================================================================
// Drums Engine - Moteur temps reel modulaire pour percussion robotique
// ============================================================================
//
// Architecture en 6 couches :
//   [MIDI Layer]      <- AppleMIDI/rtpMIDI input
//         |
//   [Event Engine]    <- Pipeline compile, conditions, transformations
//         |
//   [Scheduler]       <- Queue statique, timestamps microsecondes
//         |
//   [Actuator Engine] <- Classes Actuator (Servo, Solenoid, Motor)
//         |
//   [HAL Drivers]     <- MCP23017, PCA9685, GPIO, LEDC
//
// Principes :
//   - Zero allocation dynamique en runtime critique
//   - Lookup O(1) note -> pipeline
//   - Latence cible : 1-2 ms stable
//   - Jitter < 500 us
//   - Modulaire et extensible
//
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Wire.h>

// Core
#include "core/config.h"
#include "core/types.h"
#include "core/global_state.h"

// HAL
#include "hal/i2c_scanner.h"
#include "hal/mcp23017_driver.h"
#include "hal/pca9685_driver.h"
#include "hal/gpio_driver.h"
#include "hal/ledc_driver.h"

// Actuator
#include "actuator/actuator_manager.h"
#include "actuator/solenoid_actuator.h"
#include "actuator/servo_actuator.h"
#include "actuator/motor_actuator.h"

// Scheduler
#include "scheduler/scheduler.h"

// Event Engine
#include "event/event_processor.h"

// Instrument
#include "instrument/instrument_manager.h"

// MIDI
#include "midi/midi_engine.h"

// Loop
#include "loop/loop_engine.h"

// Storage
#include "storage/storage.h"

// Web
#include "web/web_server.h"

// ============================================================================
// Instances globales
// ============================================================================

// HAL Drivers
MCP23017Driver mcpDrivers[MCP_MAX_MODULES] = {
  MCP23017Driver(MCP_BASE_ADDRESS),
  MCP23017Driver(MCP_BASE_ADDRESS + 1),
  MCP23017Driver(MCP_BASE_ADDRESS + 2),
  MCP23017Driver(MCP_BASE_ADDRESS + 3)
};

PCA9685Driver pcaDrivers[PCA_MAX_MODULES] = {
  PCA9685Driver(PCA_BASE_ADDRESS),
  PCA9685Driver(PCA_BASE_ADDRESS + 1),
  PCA9685Driver(PCA_BASE_ADDRESS + 2),
  PCA9685Driver(PCA_BASE_ADDRESS + 3)
};

GpioDriver gpioDriver;
LedcDriver ledcDriver;

// Compteurs de modules detectes
uint8_t mcpCount = 0;
uint8_t pcaCount = 0;

// Engine Core
ActuatorManager actuatorManager;
Scheduler scheduler(&actuatorManager);
EngineState engineState;
EventProcessor eventProcessor(&scheduler, &engineState);

// Application
InstrumentManager instrumentManager(&actuatorManager, &scheduler);
MidiEngine midiEngine(&eventProcessor, &instrumentManager);
LoopEngine loopEngine(&instrumentManager);
Storage storage;
WebServerManager webServer(&instrumentManager, &actuatorManager,
                            &loopEngine, &midiEngine, &storage,
                            &scheduler, &eventProcessor);

// ============================================================================
// WiFi connection
// ============================================================================
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

// ============================================================================
// Initialiser les drivers HAL et detecter les modules I2C
// ============================================================================
void initHardware() {
  I2CScanner::begin();

  DBGLN("[HW] Scanning I2C bus...");

  // Initialiser les MCP23017 detectes
  mcpCount = 0;
  for (uint8_t i = 0; i < MCP_MAX_MODULES; i++) {
    if (mcpDrivers[i].begin()) {
      mcpCount++;
    }
  }

  // Initialiser les PCA9685 detectes
  pcaCount = 0;
  for (uint8_t i = 0; i < PCA_MAX_MODULES; i++) {
    if (pcaDrivers[i].begin()) {
      pcaCount++;
    }
  }

  // GPIO et LEDC toujours disponibles
  gpioDriver.begin();
  ledcDriver.begin();

  DBGF("[HW] Found %d MCP23017 + %d PCA9685\n", mcpCount, pcaCount);
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  #if DEBUG_SERIAL
  Serial.begin(DEBUG_BAUD);
  delay(500);
  #endif

  DBGLN("============================================");
  DBGLN("  Drums Engine - Modular RT Percussion");
  DBGLN("============================================");
  DBGLN("  Architecture: 6-layer pipeline");
  DBGLN("  HAL -> Actuator -> Scheduler");
  DBGLN("  -> Event Engine -> MIDI -> Web");
  DBGLN("============================================");

  // 1. Storage (LittleFS)
  if (!storage.begin()) {
    DBGLN("[FATAL] Storage init failed!");
  }

  // 2. Hardware (I2C, GPIO, LEDC)
  initHardware();

  if (mcpCount == 0 && pcaCount == 0) {
    DBGLN("[WARN] No I2C actuator modules found");
    DBGLN("[WARN] System will work in config-only mode");
  }

  // 3. Scheduler
  scheduler.begin();

  // 4. Load saved instruments
  if (!storage.loadInstruments(instrumentManager)) {
    DBGLN("[INFO] No saved instruments, starting fresh");
  } else {
    DBGF("[INFO] Loaded %d instruments\n", instrumentManager.getInstrumentCount());
  }

  // 5. WiFi
  if (!connectWiFi()) {
    DBGLN("[FATAL] WiFi init failed!");
  }

  // 6. MIDI over WiFi
  midiEngine.begin();

  // 7. Web Server
  webServer.begin();

  DBGLN("============================================");
  DBGLN("  Engine Ready!");
  DBGF("  Web UI:       http://%s.local\n", WIFI_HOSTNAME);
  DBGF("  MIDI:         %s (port %d)\n", MIDI_SESSION_NAME, RTP_MIDI_PORT);
  DBGF("  Instruments:  %d\n", instrumentManager.getInstrumentCount());
  DBGF("  Actuators:    %d\n", actuatorManager.getCount());
  DBGF("  Pipelines:    %d\n", eventProcessor.getLookup().pipeline_count);
  DBGF("  MCP modules:  %d\n", mcpCount);
  DBGF("  PCA modules:  %d\n", pcaCount);
  DBGLN("============================================");
}

// ============================================================================
// Main loop
// ============================================================================
void loop() {
  // 1. Recevoir MIDI (priorite haute)
  midiEngine.update();

  // 2. Scheduler: dispatcher commandes pretes (priorite tres haute)
  scheduler.update();

  // 3. Loop playback
  loopEngine.update();

  // 4. WebSocket cleanup (priorite basse)
  webServer.update();
}
