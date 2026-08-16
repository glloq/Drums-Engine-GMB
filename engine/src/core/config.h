#ifndef ENGINE_CONFIG_H
#define ENGINE_CONFIG_H

#include <Arduino.h>

// ============================================================================
// Drums Engine - Configuration Constants
// ============================================================================
// Architecture en 6 couches :
//   [MIDI Layer] -> [Event Engine] -> [Compiled Pipeline Tables]
//   -> [Scheduler] -> [Actuator Engine] -> [HAL Drivers]
// ============================================================================

// --- WiFi ---
// Credentials are loaded from /wifi.json on LittleFS (see WiFiManager).
// Only AP fallback defaults remain hardcoded here.
#define WIFI_AP_SSID      "MidiPerc-Setup"
#define WIFI_AP_PASSWORD  "midiperc"
#define WIFI_HOSTNAME     "midipercussion"

// --- Web Server ---
#define WEB_PORT          80
#define WS_PATH           "/ws"

// --- MIDI ---
#define MIDI_CHANNEL      10
#define MIDI_BPM_DEFAULT  120
#define RTP_MIDI_PORT     5004
#define MIDI_SESSION_NAME "MidiPercussion"

// --- Board pins driven by the engine itself ---
// Declared here (rather than next to their driver) so that the reserved-pin
// table in actuator/actuator_validation.cpp — which the config validator and
// the "Cablage" page both consume — has a single source of truth.
#define STATUS_LED_PIN    2        // LED integree ESP32 DevKit
#define BOOT_BUTTON_PIN   0        // Bouton BOOT ESP32 (strapping pin)

// --- I2C Hardware ---
#define I2C_SDA           21
#define I2C_SCL           22
#define I2C_FREQ          400000

#define MCP_MAX_MODULES   4
#define MCP_BASE_ADDRESS  0x20
#define PCA_MAX_MODULES   4
#define PCA_BASE_ADDRESS  0x40
#define PCA_FREQUENCY     50

// --- Engine Limits (compile-time, zero dynamic allocation) ---
// A General MIDI percussion map spans notes 35..81 (47 articulations), and the
// GM_DRUM_NOTES table in core/types.h already lists 55. The previous limit of 16
// instruments / 32 pipelines could not represent a complete kit, so a user had
// to choose which articulations to drop. The limits below carry a full GM kit
// on one board, plus room for a second channel of tuned/hand percussion.
//
// These limits are NOT free: every one of them sizes a statically allocated
// table. The ESP32's dram0_0_seg had about 31 kB of headroom before this raise,
// and whatever is left over after .bss becomes the heap the async web server
// and the WiFi stack draw from. docs/memory-budget.md itemises the cost and
// explains how to size a further raise; `pio run -e esp32 --target size` (run by
// CI) is the number that decides, not an estimate.
//
// MAX_PIPELINES is deliberately EQUAL to MAX_INSTRUMENTS. Both compilation
// paths (main.cpp compilePipelines() and PipelineCompiler::compileAll()) emit at
// most one pipeline per enabled instrument, so any pipeline slot beyond
// MAX_INSTRUMENTS can never be filled — it is 198 bytes of dead .bss each. The
// static_assert in core/types.h keeps the two in step.
#define MAX_ACTUATORS        64
#define MAX_INSTRUMENTS      64
#define MAX_ACTUATORS_PER_INST 8
#define MAX_PIPELINES        MAX_INSTRUMENTS
#define MAX_BLOCKS_PER_PIPELINE 8
#define MAX_GLOBAL_VARS      128
#define MAX_ACTIONS_PER_EVENT 4
#define MAX_CC_BINDINGS       4
#define MAX_CC_ROUTES         96
#define MAX_LOOPS            8
#define MAX_LOOP_EVENTS      256

// --- MIDI routing ---
// Notes are routed on (channel, note), not on note alone: the same note number
// on two channels is two different instruments. Channels are 1..16 on the wire
// and stored 0-based, so channel N lives at row N-1. midiChannel == 0 means
// OMNI and is expanded across every row when the lookup table is built.
#define MIDI_CHANNEL_COUNT   16

// --- Scheduler ---
#define COMMAND_QUEUE_SIZE   128
#define SCHEDULER_TICK_HZ    1000     // 1 kHz tick rate

// --- Timing ---
#define SOLENOID_STRIKE_MIN_US   8000    // 8ms min strike
#define SOLENOID_STRIKE_MAX_US   30000   // 30ms max strike
#define MIN_RETRIGGER_US         20000   // 20ms anti-bounce
#define SERVO_MOVE_TIME_US       50000   // 50ms default servo transit

// --- Safety ---
#define SOLENOID_MAX_ON_US       500000    // 500ms max solenoid activation (thermal)
#define MOTOR_MAX_CONTINUOUS_US  5000000   // 5s max continuous motor run
#define WATCHDOG_CHECK_US        100000    // Thermal/duration watchdog every 100ms (~10 Hz)
#define ENDSTOP_CHECK_US         1000      // End-stop polling every 1ms (~1 kHz, review #5)

// --- Power budget (supply protection) ---
// A flat "max 8 actuators at once" limit protects the supply by proxy: it
// assumes every actuator draws the same current. It does not — a 2 A bass-drum
// solenoid and a 20 mA servo used to count the same. The engine now arbitrates
// on the actual current draw declared per actuator (ActuatorConfig::currentMa),
// and keeps the count limit as a second, independent ceiling.
//
// Defaults are deliberately permissive on current (0 = "no current ceiling") so
// an existing configuration behaves EXACTLY as before the change until the user
// declares a supply. The count limit keeps its historical value.
#define MAX_CONCURRENT_ACTIVE       8      // Default count ceiling (0 = unlimited)
#define POWER_BUDGET_PEAK_MA_DEF    0      // Default hard current ceiling (0 = off)
#define POWER_BUDGET_CONT_MA_DEF    0      // Default sustained ceiling  (0 = off)

// --- Stepper ---
// Step pulses are generated from the RT core service loop, which runs at
// ~1 kHz (vTaskDelay(1) in rtCoreTask). Each pass may emit a bounded burst so a
// stepper can exceed the service rate without ever monopolising the RT core:
// STEPPER_MAX_STEPS_PER_PASS pulses x STEPPER_PULSE_US of busy-wait is the
// worst case added to one RT pass.
#define STEPPER_PULSE_US            4      // STEP high time (DRV8825/A4988 need >=2us)
#define STEPPER_MAX_STEPS_PER_PASS  8      // => ~8 kHz ceiling, ~32us busy-wait per pass
#define STEPPER_MAX_SPEED_SPS       8000   // Hard clamp on configured steps/second
#define STEPPER_HOMING_TIMEOUT_US   8000000 // 8s to reach the home end stop, then abort

// --- Microphone (INMP441 via I2S) ---
#define MIC_I2S_NUM          I2S_NUM_0    // I2S peripheral number
#define MIC_I2S_SCK          26           // Serial Clock (BCLK)
#define MIC_I2S_WS           25           // Word Select (LRCLK)
#define MIC_I2S_SD           33           // Serial Data (DOUT)
#define MIC_SAMPLE_RATE      44100
#define MIC_DMA_BUF_COUNT    4
#define MIC_DMA_BUF_LEN      256
#define MIC_DEFAULT_THRESHOLD 500         // Default trigger threshold (amplitude)

// --- LED Strips (WS2812B via RMT) ---
#define LED_MAX_STRIPS       4          // Max physical LED strips (1 RMT channel each)
#define LED_MAX_SEGMENTS     16         // Max LED segments (assigned to instruments)
#define LED_MAX_PIXELS       256        // Max total pixels across all strips
#define LED_MAX_THEMES       8          // Max saved themes
#define LED_THEME_PALETTE_SIZE 8        // Colors per theme palette
#define LED_FPS              60         // LED engine refresh rate
#define LED_EVENT_QUEUE_SIZE 32         // Lock-free event queue Core1→Core0

// Default LED GPIO pins (SN74HCT125 level shifter recommended)
#define LED_DEFAULT_PIN_0    4          // Strip 0 data pin
#define LED_DEFAULT_PIN_1    5          // Strip 1 data pin
#define LED_DEFAULT_PIN_2    16         // Strip 2 data pin
#define LED_DEFAULT_PIN_3    17         // Strip 3 data pin

// --- Storage ---
#define CONFIG_FILE       "/config.json"
#define ACTUATORS_FILE    "/actuators.json"
#define INSTRUMENTS_FILE  "/instruments.json"
#define LEDS_FILE         "/leds.json"
#define POWER_FILE        "/power.json"
#define THEMES_FILE       "/themes.json"
#define LOOPS_DIR         "/loops"

// --- Debug ---
// DEBUG_SERIAL can be overridden from the build (e.g. -DDEBUG_SERIAL=0 for the
// native/unit-test environment, which has no Serial).
#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL      true
#endif
#define DEBUG_BAUD        115200

#if DEBUG_SERIAL
  #define DBG(x)    Serial.print(x)
  #define DBGLN(x)  Serial.println(x)
  #define DBGF(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG(x)
  #define DBGLN(x)
  #define DBGF(...)
#endif

#endif // ENGINE_CONFIG_H
