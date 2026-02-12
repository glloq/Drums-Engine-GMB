#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// MidiPercussion V4 - ESP32 WROOM Configuration
// ============================================================================

// --- WiFi ---
#define WIFI_SSID         "MidiPercussion"    // Change to your network
#define WIFI_PASSWORD     "music1234"         // Change to your password
#define WIFI_AP_FALLBACK  true                // Create AP if STA fails
#define WIFI_AP_SSID      "MidiPerc-Setup"
#define WIFI_AP_PASSWORD  "midiperc"
#define WIFI_HOSTNAME     "midipercussion"    // mDNS: midipercussion.local

// --- Web Server ---
#define WEB_PORT          80
#define WS_PATH           "/ws"               // WebSocket endpoint

// --- MIDI ---
#define MIDI_CHANNEL      10                  // GM Drums = channel 10 (0=all)
#define MIDI_BPM_DEFAULT  120
#define RTP_MIDI_PORT     5004                // Standard rtpMIDI port
#define MIDI_SESSION_NAME "MidiPercussion"
#define ENABLE_BLE_MIDI   false               // BLE MIDI (optional)

// --- I2C Hardware ---
#define I2C_SDA           21                  // ESP32 default SDA
#define I2C_SCL           22                  // ESP32 default SCL
#define I2C_FREQ          400000              // 400kHz fast mode

#define MCP_MAX_MODULES   4                   // Max MCP23017 modules
#define MCP_BASE_ADDRESS  0x20                // 0x20-0x27
#define PCA_MAX_MODULES   4                   // Max PCA9685 modules
#define PCA_BASE_ADDRESS  0x40                // 0x40-0x7F
#define PCA_FREQUENCY     50                  // 50Hz for servos

// --- Timing ---
#define SOLENOID_STRIKE_MIN_MS   8            // Lightest strike
#define SOLENOID_STRIKE_MAX_MS   30           // Hardest strike
#define MIN_RETRIGGER_MS         20           // Anti-bounce
#define SCHEDULER_MAX_EVENTS     64           // Event pool size
#define SERVO_MOVE_TIME_MS       50           // Default servo transit time

// --- Instruments ---
#define MAX_INSTRUMENTS          16           // Max instruments
#define MAX_ACTUATORS_PER_INST   8            // Max actuators per instrument
#define MAX_LOOPS                8            // Max stored loops
#define MAX_LOOP_EVENTS          256          // Max events per loop

// --- Storage ---
#define CONFIG_FILE       "/config.json"      // System config
#define INSTRUMENTS_FILE  "/instruments.json" // Instrument definitions
#define LOOPS_DIR         "/loops"            // Loop files directory

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

#endif // CONFIG_H
