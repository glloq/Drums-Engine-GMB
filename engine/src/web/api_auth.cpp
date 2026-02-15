#include "api_auth.h"
#include <esp_random.h>

// ============================================================================
// ApiAuth implementation
// ============================================================================

bool ApiAuth::begin() {
  if (_loadToken()) {
    DBGF("[Auth] Token loaded from %s\n", AUTH_TOKEN_FILE);
    return true;
  }

  DBGLN("[Auth] No token found, generating new one");
  if (_generateAndSaveToken()) {
    DBGF("[Auth] New token saved to %s\n", AUTH_TOKEN_FILE);
    return true;
  }

  DBGLN("[Auth] ERROR: failed to generate/save token");
  return false;
}

bool ApiAuth::checkAuth(AsyncWebServerRequest* req) {
  // GET requests are read-only, no auth required
  if (req->method() == HTTP_GET) {
    return true;
  }

  // Mutating methods (POST, PUT, DELETE) require Bearer token
  if (!req->hasHeader("Authorization")) {
    req->send(401, "application/json",
              "{\"error\":\"Missing Authorization header\"}");
    return false;
  }

  String authHeader = req->header("Authorization");

  // Expect "Bearer <token>"
  if (!authHeader.startsWith("Bearer ")) {
    req->send(401, "application/json",
              "{\"error\":\"Invalid Authorization format, expected Bearer token\"}");
    return false;
  }

  String provided = authHeader.substring(7);
  provided.trim();

  if (provided.length() != AUTH_TOKEN_LEN ||
      !provided.equalsIgnoreCase(_token)) {
    req->send(401, "application/json",
              "{\"error\":\"Invalid API token\"}");
    return false;
  }

  return true;
}

String ApiAuth::getToken() const {
  return String(_token);
}

// --- Private helpers --------------------------------------------------------

bool ApiAuth::_loadToken() {
  if (!LittleFS.exists(AUTH_TOKEN_FILE)) {
    return false;
  }

  File f = LittleFS.open(AUTH_TOKEN_FILE, "r");
  if (!f) {
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    DBGF("[Auth] JSON parse error: %s\n", err.c_str());
    return false;
  }

  const char* token = doc["token"];
  if (!token || strlen(token) != AUTH_TOKEN_LEN) {
    DBGLN("[Auth] Invalid token length in file");
    return false;
  }

  // Validate hex characters
  for (size_t i = 0; i < AUTH_TOKEN_LEN; i++) {
    char c = token[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      DBGLN("[Auth] Token contains non-hex character");
      return false;
    }
  }

  memcpy(_token, token, AUTH_TOKEN_LEN);
  _token[AUTH_TOKEN_LEN] = '\0';

  return true;
}

bool ApiAuth::_generateAndSaveToken() {
  // Generate 8 random bytes -> 16 hex characters
  static const char hexChars[] = "0123456789abcdef";

  for (size_t i = 0; i < AUTH_TOKEN_LEN; i += 8) {
    uint32_t rnd = esp_random();
    size_t remaining = AUTH_TOKEN_LEN - i;
    size_t count = (remaining < 8) ? remaining : 8;

    for (size_t j = 0; j < count; j++) {
      _token[i + j] = hexChars[(rnd >> (j * 4)) & 0x0F];
    }
  }
  _token[AUTH_TOKEN_LEN] = '\0';

  // Save to LittleFS
  File f = LittleFS.open(AUTH_TOKEN_FILE, "w");
  if (!f) {
    DBGLN("[Auth] Failed to open auth file for writing");
    return false;
  }

  JsonDocument doc;
  doc["token"] = _token;

  size_t written = serializeJson(doc, f);
  f.close();

  return written > 0;
}

// ============================================================================
// RateLimiter implementation
// ============================================================================

RateLimiter::RateLimiter() {
  memset(_clients, 0, sizeof(_clients));
}

bool RateLimiter::checkRate(AsyncWebServerRequest* req) {
  uint32_t ip = (uint32_t)req->client()->remoteIP();
  uint32_t nowMs = millis();

  uint8_t idx = _findSlot(ip, nowMs);
  ClientRate& client = _clients[idx];

  // Reset window if expired
  if (nowMs - client.windowStartMs >= RATE_WINDOW_MS) {
    client.ip = ip;
    client.requestCount = 0;
    client.windowStartMs = nowMs;
  }

  client.requestCount++;

  if (client.requestCount > RATE_LIMIT_PER_S) {
    req->send(429, "application/json",
              "{\"error\":\"Too Many Requests\"}");
    return false;
  }

  return true;
}

uint8_t RateLimiter::_findSlot(uint32_t ip, uint32_t nowMs) {
  uint8_t oldestIdx = 0;
  uint32_t oldestAge = 0;

  for (uint8_t i = 0; i < MAX_RATE_CLIENTS; i++) {
    // Exact IP match: reuse this slot
    if (_clients[i].ip == ip) {
      return i;
    }

    // Empty slot (never used)
    if (_clients[i].ip == 0 && _clients[i].windowStartMs == 0) {
      return i;
    }

    // Track the oldest (most stale) entry for eviction
    uint32_t age = nowMs - _clients[i].windowStartMs;
    if (age > oldestAge) {
      oldestAge = age;
      oldestIdx = i;
    }
  }

  // No free slot and no IP match: evict the oldest entry
  _clients[oldestIdx].ip = ip;
  _clients[oldestIdx].requestCount = 0;
  _clients[oldestIdx].windowStartMs = nowMs;

  return oldestIdx;
}
