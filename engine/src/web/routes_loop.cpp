#include "web_server.h"

// --- Loops ---

void WebServerManager::_handleGetLoops(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < _loopEngine->getLoopCount(); i++) {
    Loop* loop = _loopEngine->getLoop(i);
    if (loop) {
      JsonObject obj = arr.add<JsonObject>();
      _loopEngine->loopToJson(*loop, obj);
    }
  }
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleGetLoop(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  Loop* loop = _loopEngine->getLoop(id);
  if (!loop) { _sendError(req, 404, "Loop not found"); return; }

  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  _loopEngine->loopToJson(*loop, obj);
  _sendJson(req, 200, doc);
}

void WebServerManager::_handleCreateLoop(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }

  const char* name = doc["name"] | "New Loop";
  uint16_t bpm = doc["bpm"] | MIDI_BPM_DEFAULT;
  uint8_t bars = doc["bars"] | 1;
  uint8_t beatsPerBar = doc["beatsPerBar"] | 4;
  uint8_t beatValue = doc["beatValue"] | 4;

  // P1 #12: validate up-front so the client gets a precise 400 reason
  // (createLoop also validates, but only returns a generic failure).
  const char* verr = nullptr;
  if (!LoopEngine::validateLoopParams(bpm, bars, beatsPerBar, beatValue, 96, verr)) {
    _sendError(req, 400, verr);
    return;
  }

  int idx = _loopEngine->createLoop(name, bpm, bars, beatsPerBar, beatValue);
  if (idx < 0) { _sendError(req, 507, "Max loops reached"); return; }

  JsonArray events = doc["events"].as<JsonArray>();
  if (!events.isNull()) {
    for (JsonArray evt : events) {
      _loopEngine->addEvent(idx, evt[0] | 0, evt[1] | 36, evt[2] | 80, evt[3] | 10);
    }
  }

  // Persist to LittleFS (review #8: check the result, roll back on failure).
  Loop* createdLoop = _loopEngine->getLoop(idx);
  if (createdLoop) {
    JsonDocument loopDoc;
    JsonObject loopObj = loopDoc.to<JsonObject>();
    _loopEngine->loopToJson(*createdLoop, loopObj);
    if (!_storage->saveLoop(idx, loopObj)) {
      _loopEngine->deleteLoop(idx);   // roll back the in-RAM loop
      _sendError(req, 500, "Failed to persist loop");
      return;
    }
  }

  JsonDocument resp;
  resp["index"] = idx;
  resp["message"] = "Loop created";
  _sendJson(req, 201, resp);
}

void WebServerManager::_handleUpdateLoop(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  uint8_t id = _extractId(req, "id");
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }

  Loop* loop = _loopEngine->getLoop(id);
  if (!loop) { _sendError(req, 404, "Loop not found"); return; }

  // P1 #12: validate the incoming time signature before applying it.
  uint16_t bpm = doc["bpm"] | MIDI_BPM_DEFAULT;
  uint8_t bars = doc["bars"] | 1;
  uint8_t beatsPerBar = doc["beatsPerBar"] | 4;
  uint8_t beatValue = doc["beatValue"] | 4;
  uint16_t ppq = doc["ppq"] | 96;
  const char* verr = nullptr;
  if (!LoopEngine::validateLoopParams(bpm, bars, beatsPerBar, beatValue, ppq, verr)) {
    _sendError(req, 400, verr);
    return;
  }

  _loopEngine->loopFromJson(doc.as<JsonObject>(), *loop);

  // Persist to LittleFS (review #8: surface persistence failures).
  JsonDocument loopDoc;
  JsonObject loopObj = loopDoc.to<JsonObject>();
  _loopEngine->loopToJson(*loop, loopObj);
  if (!_storage->saveLoop(id, loopObj)) {
    _sendError(req, 500, "Loop updated in RAM but persistence failed");
    return;
  }

  _sendError(req, 200, "Updated");
}

void WebServerManager::_handleDeleteLoop(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  uint8_t oldCount = _loopEngine->getLoopCount();
  if (!_loopEngine->deleteLoop(id)) { _sendError(req, 404, "Loop not found"); return; }

  // review (loop-ID stability): LoopEngine compacts the array and renumbers the
  // remaining loops' ids to their new indices. The on-disk files must follow,
  // otherwise reboot reloads stale/misaligned ids. Rewrite every remaining loop
  // to its (new) index-named file, then drop the now-orphan highest file.
  uint8_t newCount = _loopEngine->getLoopCount();
  bool ok = true;
  for (uint8_t i = 0; i < newCount; i++) {
    Loop* lp = _loopEngine->getLoop(i);
    if (!lp) continue;
    JsonDocument d;
    JsonObject o = d.to<JsonObject>();
    _loopEngine->loopToJson(*lp, o);
    ok = _storage->saveLoop(i, o) && ok;
  }
  if (oldCount > 0) _storage->deleteLoop(oldCount - 1);  // remove the trailing orphan

  if (!ok) {
    _sendError(req, 500, "Loop removed but re-persisting the remaining loops failed");
    return;
  }
  _sendError(req, 200, "Deleted");
}

void WebServerManager::_handlePlayLoop(AsyncWebServerRequest* req) {
  uint8_t id = _extractId(req, "id");
  // P1 #10: don't report success if the loop can't start (invalid/empty index
  // or panic latched).
  if (!_loopEngine->play(id)) {
    _sendError(req, 409, "Loop could not start (invalid/empty loop or panic active)");
    return;
  }
  _sendError(req, 200, "Playing");
}

void WebServerManager::_handleStopLoop(AsyncWebServerRequest* req) {
  _loopEngine->stop();
  _sendError(req, 200, "Stopped");
}

void WebServerManager::_handlePlayChain(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) { _sendError(req, 400, "Invalid JSON"); return; }

  JsonArray ids = doc["loopIds"].as<JsonArray>();
  if (ids.isNull() || ids.size() == 0) { _sendError(req, 400, "loopIds required"); return; }

  uint8_t chain[MAX_LOOPS];
  uint8_t count = 0;
  for (JsonVariant v : ids) {
    if (count >= MAX_LOOPS) break;
    chain[count++] = v.as<uint8_t>();
  }

  bool repeat = doc["repeat"] | true;
  // P1 #10: playChain validates the first loop ID (and panic state) and returns
  // false if the chain can't actually start.
  if (!_loopEngine->playChain(chain, count, repeat)) {
    _sendError(req, 409, "Chain could not start (invalid/empty first loop or panic active)");
    return;
  }

  JsonDocument resp;
  resp["message"] = "Chain started";
  resp["count"] = count;
  resp["repeat"] = repeat;
  _sendJson(req, 200, resp);
}
