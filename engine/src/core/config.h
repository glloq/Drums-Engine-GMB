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
#define WIFI_SSID         "MidiPercussion"
#define WIFI_PASSWORD     "music1234"
#define WIFI_AP_FALLBACK  true
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
#define MAX_GLOBAL_VARS      16
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

// --- Storage ---
#define CONFIG_FILE       "/config.json"
#define INSTRUMENTS_FILE  "/instruments.json"
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
