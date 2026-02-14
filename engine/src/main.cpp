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
// FreeRTOS Tasks :
//   Core 1: Scheduler (tres haute) + MIDI RX (haute) + EventProcessor
//   Core 0: Web server (basse) + Loop playback
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
#include <esp_timer.h>

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
#include "actuator/actuator_factory.h"

// Scheduler
#include "scheduler/scheduler.h"

// Event Engine
#include "event/event_processor.h"
#include "event/pipeline_compiler.h"

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
// HAL Drivers (statiques)
// ============================================================================

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

uint8_t mcpCount = 0;
uint8_t pcaCount = 0;

// ============================================================================
// Engine Core (statique, zero allocation dynamique)
// ============================================================================

ActuatorManager actuatorManager;
Scheduler scheduler(&actuatorManager);
EngineState engineState;
EventProcessor eventProcessor(&scheduler, &engineState);

// Factory pour creer les actionneurs depuis les configs
ActuatorFactory actuatorFactory(&actuatorManager,
                                 mcpDrivers, 0,   // mcpCount mis a jour apres init
                                 pcaDrivers, 0,
                                 &gpioDriver, &ledcDriver);

// Application
InstrumentManager instrumentManager(&actuatorManager, &scheduler);
MidiEngine midiEngine(&eventProcessor, &instrumentManager);
LoopEngine loopEngine(&instrumentManager);
Storage storage;
WebServerManager webServer(&instrumentManager, &actuatorManager,
                            &actuatorFactory,
                            &loopEngine, &midiEngine, &storage,
                            &scheduler, &eventProcessor);

// ============================================================================
// Hardware Timer pour Scheduler (Option B - adaptatif)
// ============================================================================

static esp_timer_handle_t schedulerTimer = nullptr;
static volatile bool schedulerTimerFired = false;

static void IRAM_ATTR schedulerTimerCallback(void* arg) {
  schedulerTimerFired = true;
}

void setupSchedulerTimer() {
  esp_timer_create_args_t timerArgs = {};
  timerArgs.callback = schedulerTimerCallback;
  timerArgs.name = "scheduler";

  if (esp_timer_create(&timerArgs, &schedulerTimer) == ESP_OK) {
    // Timer periodique a 1 kHz (1000 us)
    esp_timer_start_periodic(schedulerTimer, 1000000 / SCHEDULER_TICK_HZ);
    DBGF("[Timer] Hardware timer started at %d Hz\n", SCHEDULER_TICK_HZ);
  } else {
    DBGLN("[Timer] Failed to create hardware timer!");
  }
}

// ============================================================================
// FreeRTOS Tasks
// ============================================================================

// Task Core 1: Scheduler + MIDI + Watchdog (haute priorite, temps reel)
void rtCoreTask(void* param) {
  DBGLN("[RTOS] RT Core task started (Core 1)");
  uint32_t lastWatchdogCheck = 0;

  for (;;) {
    // Recevoir MIDI
    midiEngine.update();

    // Scheduler: dispatcher commandes pretes
    if (schedulerTimerFired) {
      schedulerTimerFired = false;
      scheduler.update();
    }

    // Watchdog: verifier les timeouts des actionneurs periodiquement
    uint32_t now = micros();
    if (now - lastWatchdogCheck >= WATCHDOG_CHECK_US) {
      lastWatchdogCheck = now;
      actuatorManager.checkWatchdog();
    }

    // Petit yield pour ne pas affamer le watchdog
    vTaskDelay(1);
  }
}

// Task Core 0: Web + Loop (basse priorite)
void appCoreTask(void* param) {
  DBGLN("[RTOS] App Core task started (Core 0)");
  for (;;) {
    // Loop playback
    loopEngine.update();

    // WebSocket cleanup
    webServer.update();

    vTaskDelay(1);
  }
}

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

  mcpCount = 0;
  for (uint8_t i = 0; i < MCP_MAX_MODULES; i++) {
    if (mcpDrivers[i].begin()) {
      mcpCount++;
    }
  }

  pcaCount = 0;
  for (uint8_t i = 0; i < PCA_MAX_MODULES; i++) {
    if (pcaDrivers[i].begin()) {
      pcaCount++;
    }
  }

  gpioDriver.begin();
  ledcDriver.begin();

  DBGF("[HW] Found %d MCP23017 + %d PCA9685\n", mcpCount, pcaCount);

  // Mettre a jour la factory avec les compteurs reels
  actuatorFactory = ActuatorFactory(&actuatorManager,
                                     mcpDrivers, mcpCount,
                                     pcaDrivers, pcaCount,
                                     &gpioDriver, &ledcDriver);
}

// ============================================================================
// Charger les configurations et creer les actionneurs
// ============================================================================
void loadConfiguration() {
  // Detecter le format de donnees (nouveau ou legacy V4)
  bool hasNewFormat = storage.fileExists(ACTUATORS_FILE);
  bool hasInstruments = storage.fileExists(INSTRUMENTS_FILE);

  if (hasNewFormat) {
    // -- Nouveau format: actuators.json + instruments.json separees --
    DBGLN("[Config] Loading new format...");

    // 1. Charger les configs d'actionneurs
    if (storage.loadActuators(actuatorFactory)) {
      DBGF("[Config] Loaded %d actuator configs\n", actuatorFactory.getConfigCount());
    } else {
      DBGLN("[Config] No actuator configs found");
    }

    // 2. Creer et enregistrer les actionneurs physiques
    uint8_t created = actuatorFactory.rebuildAll();
    DBGF("[Config] Created %d actuators from configs\n", created);

    // 3. Charger les instruments
    if (storage.loadInstruments(instrumentManager)) {
      DBGF("[Config] Loaded %d instruments\n", instrumentManager.getInstrumentCount());
    } else {
      DBGLN("[Config] No instruments found");
    }

  } else if (hasInstruments) {
    // -- Format legacy V4: actionneurs inline dans instruments --
    DBGLN("[Config] Detected V4 legacy format, migrating...");

    if (storage.loadLegacyV4(actuatorFactory, instrumentManager)) {
      // Creer les actionneurs physiques
      uint8_t created = actuatorFactory.rebuildAll();
      DBGF("[Config] Migration OK: %d instruments, %d actuators\n",
           instrumentManager.getInstrumentCount(), created);
    } else {
      DBGLN("[Config] V4 migration failed");
    }

  } else {
    DBGLN("[Config] No saved configuration, starting fresh");
  }

  // 4. Compiler les pipelines depuis les instruments charges
  compilePipelines();
}

// ============================================================================
// Compiler les pipelines depuis les instruments
// ============================================================================
void compilePipelines() {
  PipelineLookup& lookup = eventProcessor.getLookup();

  // Reinitialiser
  memset(lookup.note_to_pipeline, 0xFF, sizeof(lookup.note_to_pipeline));
  lookup.pipeline_count = 0;

  // Pour chaque instrument, creer un pipeline par defaut
  // (tant que l'UI de construction de pipeline n'est pas prete)
  for (uint8_t i = 0; i < instrumentManager.getInstrumentCount(); i++) {
    InstrumentConfig* inst = instrumentManager.getInstrumentByIndex(i);
    if (!inst || !inst->enabled || inst->midiNote >= 128) continue;
    if (lookup.pipeline_count >= MAX_PIPELINES) break;

    uint8_t pipeIdx = lookup.pipeline_count;
    CompiledPipeline& pipeline = lookup.pipelines[pipeIdx];
    pipeline.block_count = 0;

    // Trouver le premier actionneur actif de l'instrument
    uint8_t outputActId = 0xFF;
    for (uint8_t j = 0; j < inst->actuatorCount; j++) {
      uint8_t actId = inst->actuatorIds[j];
      Actuator* act = actuatorManager.getActuator(actId);
      if (act && act->isEnabled()) {
        outputActId = actId;

        // Generer un pipeline par defaut selon le type d'actionneur
        const ActuatorConfig& cfg = act->getConfig();

        if (cfg.type == ActuatorType::SOLENOID) {
          // Solenoide: bloc TIME_PULSE (duree depend velocite)
          CompiledBlock& pulseBlock = pipeline.blocks[pipeline.block_count];
          pulseBlock.type = (uint8_t)BlockType::TIME;
          pulseBlock.subtype = TIME_PULSE;
          pulseBlock.param1 = cfg.paramMin;  // duree min (ms)
          pulseBlock.param2 = cfg.paramMax;  // duree max (ms)
          pipeline.block_count++;

        } else if (cfg.type == ActuatorType::SERVO) {
          // Servo: bloc OUTPUT_POSITION
          CompiledBlock& posBlock = pipeline.blocks[pipeline.block_count];
          posBlock.type = (uint8_t)BlockType::OUTPUT;
          posBlock.subtype = OUT_POSITION;
          posBlock.param1 = actId;
          posBlock.param2 = 0;
          pipeline.block_count++;

        } else if (cfg.type == ActuatorType::PWM_MOTOR) {
          // Moteur: bloc OUTPUT_PWM
          CompiledBlock& pwmBlock = pipeline.blocks[pipeline.block_count];
          pwmBlock.type = (uint8_t)BlockType::OUTPUT;
          pwmBlock.subtype = OUT_PWM;
          pwmBlock.param1 = actId;
          pwmBlock.param2 = 0;
          pipeline.block_count++;
        }

        break; // Un pipeline par instrument pour V1
      }
    }

    pipeline.output_actuator_id = outputActId;

    // Mapper la note MIDI vers ce pipeline
    lookup.note_to_pipeline[inst->midiNote] = pipeIdx;
    inst->pipelineId = pipeIdx;
    lookup.pipeline_count++;
  }

  DBGF("[Pipeline] Compiled %d pipelines from %d instruments\n",
       lookup.pipeline_count, instrumentManager.getInstrumentCount());
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
  DBGLN("  Scheduler: hw timer + ring buffer");
  DBGLN("  RTOS: dual-core task separation");
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

  // 4. Load configuration + create actuators + compile pipelines
  loadConfiguration();

  // 5. Connecter le LoopEngine au pipeline system
  loopEngine.setEventProcessor(&eventProcessor);

  // 6. Hardware timer pour scheduler precis
  setupSchedulerTimer();

  // 6. WiFi
  if (!connectWiFi()) {
    DBGLN("[FATAL] WiFi init failed!");
  }

  // 7. MIDI over WiFi
  midiEngine.begin();

  // 8. Web Server
  webServer.begin();

  // 9. FreeRTOS tasks
  // RT Core: Scheduler + MIDI sur Core 1, priorite haute
  xTaskCreatePinnedToCore(rtCoreTask, "RT_Core", 8192, nullptr, 5, nullptr, 1);

  // App Core: Web + Loop sur Core 0, priorite basse
  xTaskCreatePinnedToCore(appCoreTask, "App_Core", 8192, nullptr, 2, nullptr, 0);

  DBGLN("============================================");
  DBGLN("  Engine Ready!");
  DBGF("  Web UI:       http://%s.local\n", WIFI_HOSTNAME);
  DBGF("  MIDI:         %s (port %d)\n", MIDI_SESSION_NAME, RTP_MIDI_PORT);
  DBGF("  Instruments:  %d\n", instrumentManager.getInstrumentCount());
  DBGF("  Actuators:    %d (configs: %d)\n",
       actuatorManager.getCount(), actuatorFactory.getConfigCount());
  DBGF("  Pipelines:    %d\n", eventProcessor.getLookup().pipeline_count);
  DBGF("  MCP modules:  %d\n", mcpCount);
  DBGF("  PCA modules:  %d\n", pcaCount);
  DBGF("  Scheduler:    %d Hz hw timer\n", SCHEDULER_TICK_HZ);
  DBGF("  RTOS tasks:   RT_Core (pri 5, core 1)\n");
  DBGF("                App_Core (pri 2, core 0)\n");
  DBGLN("============================================");
}

// ============================================================================
// Main loop (fallback - les taches principales sont dans FreeRTOS tasks)
// ============================================================================
void loop() {
  // Le loop Arduino sert de watchdog / fallback
  // Les taches principales tournent dans les FreeRTOS tasks
  vTaskDelay(100); // Ceder le CPU, tout est dans les tasks
}
