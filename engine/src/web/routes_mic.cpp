#include "web_server.h"

// GET /api/mic/status — returns mic connection state and current level
void WebServerManager::_handleGetMicStatus(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["connected"] = _mic && _mic->isConnected();
  doc["level"] = _mic ? _mic->getLevel() : 0;
  _sendJson(req, 200, doc);
}

// POST /api/mic/measure-latency — trigger actuator and measure sound arrival
// Body: { "actuatorId": N, "value": 80, "threshold": 500, "timeoutMs": 500 }
//
// Flow:
//   1. Arm mic trigger at threshold
//   2. Record t0 = micros()
//   3. Fire actuator via scheduler
//   4. Poll mic for trigger (blocking, max timeoutMs)
//   5. Return latencyUs = triggerTimestamp - t0
void WebServerManager::_handleMeasureLatency(AsyncWebServerRequest* req,
                                              uint8_t* data, size_t len) {
  if (!_mic || !_mic->isConnected()) {
    _sendError(req, 400, "Microphone not connected");
    return;
  }

  JsonDocument input;
  if (deserializeJson(input, data, len)) {
    _sendError(req, 400, "Invalid JSON");
    return;
  }

  uint8_t actuatorId = input["actuatorId"] | 0xFF;
  uint8_t value = input["value"] | 80;
  uint16_t threshold = input["threshold"] | MIC_DEFAULT_THRESHOLD;
  uint16_t timeoutMs = input["timeoutMs"] | 500;
  if (timeoutMs > 2000) timeoutMs = 2000;  // Cap at 2 seconds

  if (actuatorId == 0xFF) {
    _sendError(req, 400, "Missing actuatorId");
    return;
  }

  // Arm trigger
  _mic->armTrigger(threshold);

  // Fire actuator
  uint32_t t0 = micros();
  _actMgr->testActuator(actuatorId, value);

  // Poll for trigger detection (blocking but bounded)
  uint32_t deadlineUs = t0 + (uint32_t)timeoutMs * 1000;
  bool detected = false;

  while (micros() - t0 < (uint32_t)timeoutMs * 1000) {
    _mic->readPeak();  // Process I2S samples and check trigger
    if (_mic->isTriggered()) {
      detected = true;
      break;
    }
    delayMicroseconds(100);  // ~10kHz polling
  }

  _mic->disarmTrigger();

  JsonDocument doc;
  doc["detected"] = detected;
  if (detected) {
    uint32_t latencyUs = _mic->getTriggerTimestamp() - t0;
    doc["latencyUs"] = latencyUs;
    doc["latencyMs"] = (float)latencyUs / 1000.0f;
    doc["triggerPeak"] = _mic->getTriggerPeak();
  } else {
    doc["latencyUs"] = 0;
    doc["latencyMs"] = 0;
    doc["triggerPeak"] = 0;
    doc["error"] = "No sound detected within timeout";
  }
  doc["threshold"] = threshold;
  doc["actuatorId"] = actuatorId;

  _sendJson(req, 200, doc);
}
