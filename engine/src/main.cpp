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
#include <atomic>

// Core
#include "core/config.h"
#include "core/types.h"
#include "core/global_state.h"
#include "core/error_log.h"
#include "core/status_led.h"

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

// WiFi
#include "wifi/wifi_manager.h"

// Web
#include "web/web_server.h"

// ============================================================================
// Forward declarations
// ============================================================================
void initHardware();
void loadConfiguration();
void compilePipelines();
void loadLoops();
void setupSchedulerTimer();
void initTask(void* param);
void rtCoreTask(void* param);
void appCoreTask(void* param);

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

// Global I2C mutex (defined in hal_interface.h as extern)
SemaphoreHandle_t g_i2cMutex = nullptr;

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
WiFiManager wifiManager;
StatusLed statusLed;

// Boot button (GPIO 0) long-press tracking
static volatile uint32_t _bootBtnPressStart = 0;
static volatile bool _bootBtnHandled = false;
#define BOOT_LONG_PRESS_MS 3000

WebServerManager webServer(&instrumentManager, &actuatorManager,
                            &actuatorFactory,
                            &loopEngine, &midiEngine, &storage,
                            &scheduler, &eventProcessor);

// ============================================================================
// Hardware Timer pour Scheduler (Option B - adaptatif)
// ============================================================================

static esp_timer_handle_t schedulerTimer = nullptr;
static std::atomic<bool> schedulerTimerFired{false};

static void IRAM_ATTR schedulerTimerCallback(void* arg) {
  schedulerTimerFired.store(true, std::memory_order_release);
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
    if (schedulerTimerFired.load(std::memory_order_acquire)) {
      schedulerTimerFired.store(false, std::memory_order_relaxed);
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

// Task Core 0: Web + Loop + LED + Boot button (basse priorite)
void appCoreTask(void* param) {
  DBGLN("[RTOS] App Core task started (Core 0)");
  for (;;) {
    // Loop playback
    loopEngine.update();

    // WebSocket cleanup
    webServer.update();

    // DNS captif (traiter les requetes en mode AP)
    wifiManager.update();

    // LED de statut
    statusLed.update();

    // Bouton BOOT (GPIO 0) - detection appui long 3s
    bool btnPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    if (btnPressed) {
      if (_bootBtnPressStart == 0) {
        _bootBtnPressStart = millis();
        _bootBtnHandled = false;
      } else if (!_bootBtnHandled && (millis() - _bootBtnPressStart >= BOOT_LONG_PRESS_MS)) {
        _bootBtnHandled = true;
        DBGLN("[BOOT] Long press detected -> switching to AP mode");
        ErrorLog::info("Boot button: AP mode activated");
        wifiManager.switchToAP();
        statusLed.setPattern(LedPattern::AP_MODE);
      }
    } else {
      _bootBtnPressStart = 0;
      _bootBtnHandled = false;
    }

    vTaskDelay(1);
  }
}

// ============================================================================
// Initialiser les drivers HAL et detecter les modules I2C
// ============================================================================
void initHardware() {
  g_i2cMutex = xSemaphoreCreateMutex();
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

  // 5. Charger les boucles depuis le stockage
  loadLoops();
}

// ============================================================================
// Compiler les pipelines depuis les instruments
// ============================================================================
void compilePipelines() {
  PipelineLookup& lookup = eventProcessor.getLookup();

  // Reinitialiser
  memset(lookup.note_to_pipeline, 0xFF, sizeof(lookup.note_to_pipeline));
  lookup.pipeline_count = 0;
  lookup.cc_route_count = 0;
  memset(lookup.cc_to_first, 0xFF, sizeof(lookup.cc_to_first));
  memset(lookup.cc_to_count, 0, sizeof(lookup.cc_to_count));

  // Pour chaque instrument, creer un pipeline par defaut
  // (tant que l'UI de construction de pipeline n'est pas prete)
  for (uint8_t i = 0; i < instrumentManager.getInstrumentCount(); i++) {
    InstrumentConfig* inst = instrumentManager.getInstrumentByIndex(i);
    if (!inst || !inst->enabled || inst->midiNote >= 128) continue;
    if (lookup.pipeline_count >= MAX_PIPELINES) break;

    uint8_t pipeIdx = lookup.pipeline_count;
    CompiledPipeline& pipeline = lookup.pipelines[pipeIdx];
    pipeline.block_count = 0;
    pipeline.output_actuator_id = 0xFF;
    pipeline.note_on_count = 0;
    pipeline.note_off_count = 0;
    pipeline.cc_binding_count = 0;
    pipeline.retrigger_mode = inst->retriggerMode;

    // Prefer explicit action model when instrument already provides it
    if (inst->noteOnCount > 0 || inst->noteOffCount > 0) {
      pipeline.note_on_count = min((uint8_t)MAX_ACTIONS_PER_EVENT, inst->noteOnCount);
      pipeline.note_off_count = min((uint8_t)MAX_ACTIONS_PER_EVENT, inst->noteOffCount);
      pipeline.cc_binding_count = min((uint8_t)MAX_CC_BINDINGS, inst->ccBindingCount);

      for (uint8_t j = 0; j < pipeline.note_on_count; j++) {
        pipeline.note_on_actions[j] = inst->noteOnActions[j];
      }
      for (uint8_t j = 0; j < pipeline.note_off_count; j++) {
        pipeline.note_off_actions[j] = inst->noteOffActions[j];
      }
      for (uint8_t j = 0; j < pipeline.cc_binding_count; j++) {
        pipeline.cc_bindings[j] = inst->ccBindings[j];
      }
    } else {
      // Legacy migration path: generate default actions from actuator list
      for (uint8_t j = 0; j < inst->actuatorCount; j++) {
        uint8_t actId = inst->actuatorIds[j];
        Actuator* act = actuatorManager.getActuator(actId);
        if (!act || !act->isEnabled()) continue;

        const ActuatorConfig& cfg = act->getConfig();

        if (pipeline.output_actuator_id == 0xFF) {
          pipeline.output_actuator_id = actId;
        }

        if (pipeline.note_on_count < MAX_ACTIONS_PER_EVENT) {
          ActionStep& on = pipeline.note_on_actions[pipeline.note_on_count++];
          on.actuator_id = actId;
          on.value_source = (uint8_t)ValueSource::VELOCITY;
          on.delay_ms = 0;
          on.velocity_curve = CURVE_LINEAR;

          if (cfg.type == ActuatorType::SOLENOID) {
            on.command_type = (uint8_t)CommandType::PULSE;
            on.duration_min_ms = cfg.paramMin;
            on.duration_max_ms = cfg.paramMax;
          } else if (cfg.type == ActuatorType::SERVO) {
            on.command_type = (uint8_t)CommandType::POSITION;
          } else if (cfg.type == ActuatorType::PWM_MOTOR) {
            on.command_type = (uint8_t)CommandType::PWM;
          }
        }

        if (pipeline.note_off_count < MAX_ACTIONS_PER_EVENT) {
          ActionStep& off = pipeline.note_off_actions[pipeline.note_off_count++];
          off.actuator_id = actId;
          off.command_type = (uint8_t)CommandType::OFF;
          off.value_source = (uint8_t)ValueSource::FIXED;
          off.value_fixed = 0;
          off.delay_ms = 0;
        }
      }
    }

    // Mapper la note MIDI vers ce pipeline
    lookup.note_to_pipeline[inst->midiNote] = pipeIdx;
    inst->pipelineId = pipeIdx;
    lookup.pipeline_count++;
  }

  // Compiler la table de routage CC (phase 2)
  for (uint8_t cc = 0; cc < 128; cc++) {
    uint8_t first = lookup.cc_route_count;
    uint8_t count = 0;

    for (uint8_t p = 0; p < lookup.pipeline_count; p++) {
      const CompiledPipeline& pipe = lookup.pipelines[p];
      for (uint8_t i = 0; i < pipe.cc_binding_count; i++) {
        const CCBinding& binding = pipe.cc_bindings[i];
        if (binding.cc_number != cc || binding.actuator_id == 0xFF) continue;
        if (lookup.cc_route_count >= MAX_CC_ROUTES) break;

        CCRoutingEntry& route = lookup.cc_routes[lookup.cc_route_count++];
        route.actuator_id = binding.actuator_id;
        route.command_type = binding.command_type;
        route.range_min = binding.range_min;
        route.range_max = binding.range_max;
        route.curve = binding.curve;
        route.inverted = binding.inverted;
        count++;
      }
      if (lookup.cc_route_count >= MAX_CC_ROUTES) break;
    }

    if (count > 0) {
      lookup.cc_to_first[cc] = first;
      lookup.cc_to_count[cc] = count;
    }

    if (lookup.cc_route_count >= MAX_CC_ROUTES) {
      DBGLN("[Pipeline] CC route table full, truncating");
      break;
    }
  }

  DBGF("[Pipeline] Compiled %d pipelines from %d instruments, %d CC routes\n",
       lookup.pipeline_count, instrumentManager.getInstrumentCount(), lookup.cc_route_count);
}

// ============================================================================
// Charger les boucles depuis le stockage
// ============================================================================
void loadLoops() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  if (storage.listLoops(arr)) {
    for (JsonObject loopInfo : arr) {
      uint8_t id = loopInfo["id"] | 0;
      JsonDocument loopDoc;
      JsonObject loopObj = loopDoc.to<JsonObject>();
      if (storage.loadLoop(id, loopObj)) {
        const char* name = loopObj["name"] | "Loop";
        uint16_t bpm = loopObj["bpm"] | MIDI_BPM_DEFAULT;
        uint8_t bars = loopObj["bars"] | 1;
        uint8_t beatsPerBar = loopObj["beatsPerBar"] | 4;
        uint8_t beatValue = loopObj["beatValue"] | 4;

        int idx = loopEngine.createLoop(name, bpm, bars, beatsPerBar, beatValue);
        if (idx >= 0) {
          Loop* loop = loopEngine.getLoop(idx);
          if (loop) loopEngine.loopFromJson(loopObj, *loop);
        }
      }
    }
    DBGF("[Config] Loaded %d loops\n", loopEngine.getLoopCount());
  }
}

// ============================================================================
// Init Task - initialisation lourde dans une tache avec 32KB de stack
// (le loopTask Arduino n'a que 8KB, insuffisant pour WiFi+JSON+Web+MIDI)
// ============================================================================
void initTask(void* param) {
  // 1. Storage (LittleFS)
  if (!storage.begin()) {
    DBGLN("[FATAL] Storage init failed!");
    statusLed.setPattern(LedPattern::ERROR_BLINK);
  }

  // 1b. Error logging (depends on LittleFS)
  ErrorLog::begin();
  ErrorLog::info("Drums Engine starting...");

  // 2. Hardware (I2C, GPIO, LEDC)
  initHardware();

  if (mcpCount == 0 && pcaCount == 0) {
    DBGLN("[WARN] No I2C actuator modules found");
    DBGLN("[WARN] System will work in config-only mode");
  }

  // 3. Scheduler
  scheduler.begin();
  actuatorManager.setScheduler(&scheduler);

  // 4. Load configuration + create actuators + compile pipelines
  loadConfiguration();

  // 5. Connecter le LoopEngine au pipeline system
  loopEngine.setEventProcessor(&eventProcessor);

  // 6. Hardware timer pour scheduler precis
  setupSchedulerTimer();

  // 7. WiFi (credentials from /wifi.json, AP fallback)
  if (!wifiManager.begin()) {
    DBGLN("[FATAL] WiFi init failed!");
    ErrorLog::error("WiFi init failed");
    statusLed.setPattern(LedPattern::ERROR_BLINK);
  } else {
    // LED selon le mode WiFi
    statusLed.setPattern(wifiManager.isAPMode() ? LedPattern::AP_MODE : LedPattern::CONNECTED);
  }

  // 8. MIDI over WiFi
  midiEngine.begin();

  // 9. Web Server (includes auth, rate-limiting, WiFi API, logs API)
  webServer.setRecompileCallback(compilePipelines);
  webServer.begin();
  wifiManager.registerRoutes(&webServer.getServer());

  // 10. FreeRTOS tasks
  // RT Core: Scheduler + MIDI sur Core 1, priorite haute
  xTaskCreatePinnedToCore(rtCoreTask, "RT_Core", 16384, nullptr, 5, nullptr, 1);

  // App Core: Web + Loop sur Core 0, priorite basse
  xTaskCreatePinnedToCore(appCoreTask, "App_Core", 16384, nullptr, 2, nullptr, 0);

  DBGLN("============================================");
  DBGLN("  Engine Ready!");
  DBGF("  WiFi mode:    %s\n", wifiManager.isAPMode() ? "AP" : "STA");
  DBGF("  Web UI:       http://%s.local\n", wifiManager.getHostname().c_str());
  DBGF("  IP:           %s\n", wifiManager.getIP().c_str());
  DBGF("  API Token:    %s\n", webServer.getApiToken().c_str());
  DBGF("  MIDI:         %s (port %d)\n", MIDI_SESSION_NAME, RTP_MIDI_PORT);
  DBGF("  Instruments:  %d\n", instrumentManager.getInstrumentCount());
  DBGF("  Actuators:    %d (configs: %d)\n",
       actuatorManager.getCount(), actuatorFactory.getConfigCount());
  DBGF("  Pipelines:    %d\n", eventProcessor.getLookup().pipeline_count);
  DBGF("  MCP modules:  %d\n", mcpCount);
  DBGF("  PCA modules:  %d\n", pcaCount);
  DBGF("  Scheduler:    %d Hz hw timer\n", SCHEDULER_TICK_HZ);
  DBGF("  RTOS tasks:   RT_Core (pri 5, core 1, 16KB stack)\n");
  DBGF("                App_Core (pri 2, core 0, 16KB stack)\n");
  DBGF("  LED statut:   GPIO %d (%s)\n", STATUS_LED_PIN,
       wifiManager.isAPMode() ? "clignotement lent = AP" : "fixe = connecte");
  DBGF("  Bouton BOOT:  GPIO %d (3s = hotspot)\n", BOOT_BUTTON_PIN);
  DBGF("  Free heap:    %d bytes\n", ESP.getFreeHeap());
  DBGLN("============================================");

  ErrorLog::info("Engine ready");

  // Init terminee, supprimer cette tache (libere les 32KB de stack)
  vTaskDelete(nullptr);
}

// ============================================================================
// Setup - minimal, delegue l'init lourde a une tache avec plus de stack
// ============================================================================
void setup() {
  #if DEBUG_SERIAL
  Serial.begin(DEBUG_BAUD);
  delay(500);
  #endif

  // LED de statut + bouton BOOT (leger, pas de risque de stack overflow)
  statusLed.begin();
  statusLed.setPattern(LedPattern::BOOTING);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  DBGLN("============================================");
  DBGLN("  Drums Engine - Modular RT Percussion");
  DBGLN("============================================");
  DBGLN("  Architecture: 6-layer pipeline");
  DBGLN("  Scheduler: hw timer + ring buffer");
  DBGLN("  RTOS: dual-core task separation");
  DBGLN("============================================");

  // Lancer l'initialisation lourde dans une tache dediee (32KB stack)
  // Le loopTask Arduino n'a que 8KB, insuffisant pour WiFi+JSON+Web+MIDI
  xTaskCreatePinnedToCore(initTask, "Init", 32768, nullptr, 5, nullptr, 1);
}

// ============================================================================
// Main loop (inactif - tout tourne dans les FreeRTOS tasks)
// ============================================================================
void loop() {
  vTaskDelay(portMAX_DELAY);
}
