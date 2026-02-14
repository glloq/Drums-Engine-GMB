#include "storage.h"

bool Storage::begin() {
  if (!LittleFS.begin(true)) {
    DBGLN("[Storage] LittleFS mount failed!");
    return false;
  }

  if (!LittleFS.exists(LOOPS_DIR)) {
    LittleFS.mkdir(LOOPS_DIR);
  }

  DBGF("[Storage] LittleFS OK - %s\n", formatInfo().c_str());
  return true;
}

bool Storage::saveInstruments(const InstrumentManager& mgr) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  mgr.toJson(arr);
  return _writeJson(INSTRUMENTS_FILE, doc);
}

bool Storage::loadInstruments(InstrumentManager& mgr) {
  JsonDocument doc;
  if (!_readJson(INSTRUMENTS_FILE, doc)) return false;
  JsonArray arr = doc.as<JsonArray>();
  return mgr.fromJson(arr);
}

bool Storage::saveSystemConfig(const JsonObject& cfg) {
  JsonDocument doc;
  doc.set(cfg);
  return _writeJson(CONFIG_FILE, doc);
}

bool Storage::loadSystemConfig(JsonObject& cfg) {
  JsonDocument doc;
  if (!_readJson(CONFIG_FILE, doc)) return false;
  cfg = doc.as<JsonObject>();
  return true;
}

bool Storage::saveLoop(uint8_t id, const JsonObject& loop) {
  char path[32];
  snprintf(path, sizeof(path), "%s/loop_%d.json", LOOPS_DIR, id);
  JsonDocument doc;
  doc.set(loop);
  return _writeJson(path, doc);
}

bool Storage::loadLoop(uint8_t id, JsonObject& loop) {
  char path[32];
  snprintf(path, sizeof(path), "%s/loop_%d.json", LOOPS_DIR, id);
  JsonDocument doc;
  if (!_readJson(path, doc)) return false;
  loop = doc.as<JsonObject>();
  return true;
}

bool Storage::deleteLoop(uint8_t id) {
  char path[32];
  snprintf(path, sizeof(path), "%s/loop_%d.json", LOOPS_DIR, id);
  return LittleFS.remove(path);
}

bool Storage::listLoops(JsonArray& arr) {
  File dir = LittleFS.open(LOOPS_DIR);
  if (!dir || !dir.isDirectory()) return false;

  File file = dir.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String name = file.name();
      if (name.startsWith("loop_") && name.endsWith(".json")) {
        int id = name.substring(5, name.length() - 5).toInt();
        JsonDocument doc;
        if (_readJson(file.path(), doc)) {
          JsonObject obj = arr.add<JsonObject>();
          obj["id"] = id;
          obj["name"] = doc["name"] | "Unnamed Loop";
          obj["bpm"] = doc["bpm"] | MIDI_BPM_DEFAULT;
          obj["bars"] = doc["bars"] | 1;
          obj["eventCount"] = doc["events"].as<JsonArray>().size();
        }
      }
    }
    file = dir.openNextFile();
  }
  return true;
}

size_t Storage::totalBytes() { return LittleFS.totalBytes(); }
size_t Storage::usedBytes() { return LittleFS.usedBytes(); }

String Storage::formatInfo() {
  char buf[64];
  snprintf(buf, sizeof(buf), "Used: %lu / %lu bytes (%.1f%%)",
           (unsigned long)usedBytes(), (unsigned long)totalBytes(),
           (float)usedBytes() / totalBytes() * 100.0f);
  return String(buf);
}

bool Storage::_writeJson(const char* path, const JsonDocument& doc) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    DBGF("[Storage] Failed to open %s for writing\n", path);
    return false;
  }
  size_t written = serializeJson(doc, file);
  file.close();
  DBGF("[Storage] Wrote %d bytes to %s\n", written, path);
  return written > 0;
}

bool Storage::_readJson(const char* path, JsonDocument& doc) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    DBGF("[Storage] File not found: %s\n", path);
    return false;
  }
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    DBGF("[Storage] JSON parse error in %s: %s\n", path, err.c_str());
    return false;
  }
  return true;
}
