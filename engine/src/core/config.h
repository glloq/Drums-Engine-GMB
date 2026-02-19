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
#define MAX_ACTUATORS        32
#define MAX_INSTRUMENTS      16
#define MAX_ACTUATORS_PER_INST 8
#define MAX_PIPELINES        32
#define MAX_BLOCKS_PER_PIPELINE 8
#define MAX_GLOBAL_VARS      128
#define MAX_ACTIONS_PER_EVENT 4
#define MAX_CC_BINDINGS       4
#define MAX_CC_ROUTES         16
#define MAX_LOOPS            8
#define MAX_LOOP_EVENTS      256

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
#define MAX_CONCURRENT_ACTIVE    8         // Max simultaneously active actuators
#define WATCHDOG_CHECK_US        100000    // Check watchdog every 100ms

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
#define THEMES_FILE       "/themes.json"
#define LOOPS_DIR         "/loops"

// --- Debug ---
#define DEBUG_SERIAL      true
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
