#ifndef API_AUTH_H
#define API_AUTH_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "../core/config.h"

// ============================================================================
// API Authentication & Rate Limiting
// ============================================================================
// - ApiAuth: Bearer token authentication for mutating endpoints (POST/PUT/DELETE)
//   GET and WebSocket connections are exempt (read-only / local config UI).
//   Token is persisted in /auth.json on LittleFS; auto-generated on first boot.
//   Optional PIN code protects the token endpoint (login page).
//
// - RateLimiter: Fixed-window per-IP rate limiter (no heap allocation).
//   Tracks up to MAX_RATE_CLIENTS IPs with a sliding 1-second window.
// ============================================================================

#define AUTH_TOKEN_FILE   "/auth.json"
#define AUTH_TOKEN_LEN    16          // 16 hex chars = 8 random bytes
#define AUTH_PIN_MAX_LEN  16          // max PIN length
#define MAX_RATE_CLIENTS  8
#define RATE_LIMIT_PER_S  30
#define RATE_WINDOW_MS    1000

// ============================================================================
// ApiAuth
// ============================================================================

class ApiAuth {
public:
  /// Load token from LittleFS (or generate + save on first boot).
  /// Call after LittleFS.begin().
  bool begin();

  /// Check whether the request is authorized.
  /// GET requests always pass (read-only).
  /// POST / PUT / DELETE must carry a valid Bearer token.
  /// Returns true if the request may proceed.
  /// On failure, sends a 401 response and returns false.
  bool checkAuth(AsyncWebServerRequest* req);

  /// Return the current token (for Serial display at boot).
  String getToken() const;

  /// PIN management
  bool hasPin() const;
  bool checkPin(const String& pin) const;
  bool setPin(const String& pin);

private:
  char _token[AUTH_TOKEN_LEN + 1] = {0};
  char _pin[AUTH_PIN_MAX_LEN + 1] = {0};
  bool _hasPin = false;

  bool _loadToken();
  bool _generateAndSaveToken();
  bool _saveAuthFile();
};

// ============================================================================
// RateLimiter
// ============================================================================

struct ClientRate {
  uint32_t ip;
  uint32_t requestCount;
  uint32_t windowStartMs;
};

class RateLimiter {
public:
  RateLimiter();

  /// Check whether the request is within rate limits.
  /// Returns true if allowed.
  /// On failure, sends a 429 response and returns false.
  bool checkRate(AsyncWebServerRequest* req);

private:
  ClientRate _clients[MAX_RATE_CLIENTS];

  /// Find (or evict) a slot for the given IP.
  /// Returns the index into _clients.
  uint8_t _findSlot(uint32_t ip, uint32_t nowMs);
};

#endif // API_AUTH_H
