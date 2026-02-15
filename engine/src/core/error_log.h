#ifndef ENGINE_ERROR_LOG_H
#define ENGINE_ERROR_LOG_H

#include <Arduino.h>

// ============================================================================
// ErrorLog - Persistent error/warning/info logger
// ============================================================================
// Writes timestamped entries to /error.log on LittleFS with automatic
// log rotation (max 8KB). Maintains a 16-entry in-memory ring buffer
// for fast access via /api/logs.
//
// Entry format: [UPTIME_S] [LEVEL] message\n
// Levels: ERROR=0, WARN=1, INFO=2
//
// All methods are static - call from anywhere:
//   ErrorLog::error("Something failed");
//   ErrorLog::logf(ErrorLog::WARN, "Retry %d of %d", attempt, max);
// ============================================================================

// --- Log levels ---
#define LOG_LEVEL_ERROR  0
#define LOG_LEVEL_WARN   1
#define LOG_LEVEL_INFO   2

// --- Configuration ---
#define ERROR_LOG_FILE        "/error.log"
#define ERROR_LOG_MAX_SIZE    8192        // 8KB max file size
#define ERROR_LOG_RING_SIZE   16          // In-memory ring buffer entries
#define ERROR_LOG_MSG_LEN     80          // Max message length per entry

// --- Ring buffer entry ---
struct LogEntry {
  uint32_t uptimeS;
  uint8_t  level;
  char     message[ERROR_LOG_MSG_LEN];
};

class ErrorLog {
public:
  // Initialize the logger (call after LittleFS is mounted)
  static void begin();

  // Log at specific levels
  static void error(const char* msg);
  static void warn(const char* msg);
  static void info(const char* msg);

  // Printf-style logging with level
  static void logf(uint8_t level, const char* fmt, ...);

  // Read entire log file from LittleFS
  static String readAll();

  // Clear the log file and ring buffer
  static void clear();

  // Number of entries in ring buffer (recent entries only)
  static uint16_t entryCount();

  // Access ring buffer entries (index 0 = oldest available)
  static const LogEntry* getEntries(uint16_t& count);

private:
  static void _write(uint8_t level, const char* msg);
  static void _rotateIfNeeded();
  static const char* _levelStr(uint8_t level);

  static LogEntry  _ring[ERROR_LOG_RING_SIZE];
  static uint16_t  _ringHead;     // Next write position
  static uint16_t  _ringCount;    // Number of valid entries
  static bool      _initialized;
};

#endif // ENGINE_ERROR_LOG_H
