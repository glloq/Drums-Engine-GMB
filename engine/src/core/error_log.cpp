#include "error_log.h"
#include "config.h"
#include <LittleFS.h>
#include <stdarg.h>

// ============================================================================
// ErrorLog - Implementation
// ============================================================================
// Persistent logger with LittleFS backend and in-memory ring buffer.
// Log rotation: when file exceeds 8KB, keep second half and rewrite.
// Total static RAM usage: 16 entries * (4+1+80) = 1360 bytes + overhead < 2KB
// ============================================================================

// --- Static member definitions ---
LogEntry  ErrorLog::_ring[ERROR_LOG_RING_SIZE];
uint16_t  ErrorLog::_ringHead    = 0;
uint16_t  ErrorLog::_ringCount   = 0;
bool      ErrorLog::_initialized = false;

// ============================================================================
// Public API
// ============================================================================

void ErrorLog::begin() {
  memset(_ring, 0, sizeof(_ring));
  _ringHead  = 0;
  _ringCount = 0;
  _initialized = true;

  // Create the log file if it doesn't exist
  if (!LittleFS.exists(ERROR_LOG_FILE)) {
    File f = LittleFS.open(ERROR_LOG_FILE, "w");
    if (f) f.close();
  }

  DBGLN("[ErrorLog] Initialized");
}

void ErrorLog::error(const char* msg) {
  _write(LOG_LEVEL_ERROR, msg);
}

void ErrorLog::warn(const char* msg) {
  _write(LOG_LEVEL_WARN, msg);
}

void ErrorLog::info(const char* msg) {
  _write(LOG_LEVEL_INFO, msg);
}

void ErrorLog::logf(uint8_t level, const char* fmt, ...) {
  char buf[ERROR_LOG_MSG_LEN];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  _write(level, buf);
}

String ErrorLog::readAll() {
  if (!_initialized) return String();

  File f = LittleFS.open(ERROR_LOG_FILE, "r");
  if (!f) return String();

  String content = f.readString();
  f.close();
  return content;
}

void ErrorLog::clear() {
  if (!_initialized) return;

  // Clear ring buffer
  memset(_ring, 0, sizeof(_ring));
  _ringHead  = 0;
  _ringCount = 0;

  // Truncate file
  File f = LittleFS.open(ERROR_LOG_FILE, "w");
  if (f) f.close();

  DBGLN("[ErrorLog] Log cleared");
}

uint16_t ErrorLog::entryCount() {
  return _ringCount;
}

const LogEntry* ErrorLog::getEntries(uint16_t& count) {
  count = _ringCount;
  return _ring;
}

// ============================================================================
// Internal
// ============================================================================

void ErrorLog::_write(uint8_t level, const char* msg) {
  if (!_initialized) return;

  uint32_t uptimeS = (uint32_t)(millis() / 1000);

  // --- Add to ring buffer ---
  LogEntry& entry = _ring[_ringHead];
  entry.uptimeS = uptimeS;
  entry.level   = level;
  strlcpy(entry.message, msg, ERROR_LOG_MSG_LEN);

  _ringHead = (_ringHead + 1) % ERROR_LOG_RING_SIZE;
  if (_ringCount < ERROR_LOG_RING_SIZE) {
    _ringCount++;
  }

  // --- Format the line ---
  char line[ERROR_LOG_MSG_LEN + 32];
  snprintf(line, sizeof(line), "[%lu] [%s] %s\n",
           (unsigned long)uptimeS, _levelStr(level), msg);

  // --- Mirror to Serial if debug enabled ---
#if DEBUG_SERIAL
  Serial.print(line);
#endif

  // --- Append to file ---
  File f = LittleFS.open(ERROR_LOG_FILE, "a");
  if (f) {
    f.print(line);
    f.close();
  }

  // --- Rotate if needed ---
  _rotateIfNeeded();
}

void ErrorLog::_rotateIfNeeded() {
  File f = LittleFS.open(ERROR_LOG_FILE, "r");
  if (!f) return;

  size_t fileSize = f.size();
  if (fileSize <= ERROR_LOG_MAX_SIZE) {
    f.close();
    return;
  }

  // File exceeds max size - keep the second half
  size_t halfSize = fileSize / 2;

  // Seek to the halfway point
  f.seek(halfSize);

  // Skip to the next complete line (find the next newline)
  while (f.available()) {
    char c = f.read();
    if (c == '\n') break;
  }

  // Read the remaining content (second half)
  String keepData = f.readString();
  f.close();

  // Rewrite the file with only the second half
  File fw = LittleFS.open(ERROR_LOG_FILE, "w");
  if (fw) {
    fw.print(keepData);
    fw.close();
  }

  DBGF("[ErrorLog] Rotated: %u -> %u bytes\n",
       (unsigned)fileSize, (unsigned)keepData.length());
}

const char* ErrorLog::_levelStr(uint8_t level) {
  switch (level) {
    case LOG_LEVEL_ERROR: return "ERROR";
    case LOG_LEVEL_WARN:  return "WARN";
    case LOG_LEVEL_INFO:  return "INFO";
    default:              return "???";
  }
}
