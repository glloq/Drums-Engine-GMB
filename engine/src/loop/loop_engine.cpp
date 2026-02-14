#include "loop_engine.h"

LoopEngine::LoopEngine(InstrumentManager* instrumentMgr)
  : _instrumentMgr(instrumentMgr), _loopCount(0),
    _playing(false), _paused(false), _currentLoop(0),
    _currentTick(0), _nextEventIndex(0), _lastTickTime(0),
    _tickIntervalUs(0) {}

// --- Lecture ---

void LoopEngine::play(uint8_t loopIndex) {
  if (loopIndex >= _loopCount) return;
  Loop& loop = _loops[loopIndex];
  if (loop.eventCount == 0) return;

  _currentLoop = loopIndex;
  _currentTick = 0;
  _nextEventIndex = 0;
  _playing = true;
  _paused = false;
  _lastTickTime = micros();
  _calcTickInterval(loop.bpm, loop.ppq);

  DBGF("[Loop] Playing '%s' at %d BPM (%d events)\n",
       loop.name, loop.bpm, loop.eventCount);
}

void LoopEngine::stop() {
  _playing = false;
  _paused = false;
  _currentTick = 0;
  _nextEventIndex = 0;
  DBGLN("[Loop] Stopped");
}

void LoopEngine::pause() {
  if (_playing) { _paused = true; DBGLN("[Loop] Paused"); }
}

void LoopEngine::resume() {
  if (_paused) { _paused = false; _lastTickTime = micros(); DBGLN("[Loop] Resumed"); }
}

void LoopEngine::update() {
  if (!_playing || _paused) return;
  if (_currentLoop >= _loopCount) { stop(); return; }

  Loop& loop = _loops[_currentLoop];
  unsigned long now = micros();
  unsigned long elapsed = now - _lastTickTime;

  while (elapsed >= _tickIntervalUs) {
    elapsed -= _tickIntervalUs;
    _lastTickTime += _tickIntervalUs;
    _currentTick++;

    uint16_t total = _totalTicks(loop);
    if (_currentTick >= total) {
      _currentTick = 0;
      _nextEventIndex = 0;
    }

    while (_nextEventIndex < loop.eventCount &&
           loop.events[_nextEventIndex].tickPosition == _currentTick) {
      const LoopEvent& evt = loop.events[_nextEventIndex];
      if (evt.velocity > 0) {
        _instrumentMgr->onNoteOn(evt.channel, evt.midiNote, evt.velocity);
      } else {
        _instrumentMgr->onNoteOff(evt.channel, evt.midiNote);
      }
      _nextEventIndex++;
    }
  }
}

// --- Gestion ---

int LoopEngine::createLoop(const char* name, uint16_t bpm, uint8_t bars,
                            uint8_t beatsPerBar, uint8_t beatValue) {
  if (_loopCount >= MAX_LOOPS) return -1;

  Loop& loop = _loops[_loopCount];
  loop.id = _loopCount;
  strlcpy(loop.name, name, sizeof(loop.name));
  loop.bpm = bpm;
  loop.bars = bars;
  loop.beatsPerBar = beatsPerBar;
  loop.beatValue = beatValue;
  loop.ppq = 96;
  loop.eventCount = 0;
  loop.active = true;

  _loopCount++;
  DBGF("[Loop] Created '%s' (%d BPM, %d bars, %d/%d)\n",
       name, bpm, bars, beatsPerBar, beatValue);
  return _loopCount - 1;
}

bool LoopEngine::deleteLoop(uint8_t index) {
  if (index >= _loopCount) return false;
  if (_playing && _currentLoop == index) stop();

  for (uint8_t i = index; i < _loopCount - 1; i++) {
    _loops[i] = _loops[i + 1];
    _loops[i].id = i;
  }
  _loopCount--;
  return true;
}

Loop* LoopEngine::getLoop(uint8_t index) {
  if (index >= _loopCount) return nullptr;
  return &_loops[index];
}

// --- Events ---

int LoopEngine::addEvent(uint8_t loopIndex, uint16_t tick, uint8_t note,
                          uint8_t velocity, uint8_t channel) {
  if (loopIndex >= _loopCount) return -1;
  Loop& loop = _loops[loopIndex];
  if (loop.eventCount >= MAX_LOOP_EVENTS) return -1;

  loop.events[loop.eventCount] = {tick, note, velocity, channel};
  loop.eventCount++;
  _sortEvents(loop);
  return loop.eventCount - 1;
}

bool LoopEngine::removeEvent(uint8_t loopIndex, uint16_t eventIndex) {
  if (loopIndex >= _loopCount) return false;
  Loop& loop = _loops[loopIndex];
  if (eventIndex >= loop.eventCount) return false;

  for (uint16_t i = eventIndex; i < loop.eventCount - 1; i++) {
    loop.events[i] = loop.events[i + 1];
  }
  loop.eventCount--;
  return true;
}

bool LoopEngine::clearEvents(uint8_t loopIndex) {
  if (loopIndex >= _loopCount) return false;
  _loops[loopIndex].eventCount = 0;
  return true;
}

void LoopEngine::quantize(uint8_t loopIndex, uint8_t gridDivision) {
  if (loopIndex >= _loopCount) return;
  Loop& loop = _loops[loopIndex];

  uint16_t gridTicks = (loop.ppq * 4) / gridDivision;
  for (uint16_t i = 0; i < loop.eventCount; i++) {
    uint16_t tick = loop.events[i].tickPosition;
    uint16_t remainder = tick % gridTicks;
    if (remainder > gridTicks / 2) {
      loop.events[i].tickPosition = tick + (gridTicks - remainder);
    } else {
      loop.events[i].tickPosition = tick - remainder;
    }
  }
  _sortEvents(loop);
}

// --- Serialization ---

void LoopEngine::loopToJson(const Loop& loop, JsonObject& obj) const {
  obj["id"] = loop.id;
  obj["name"] = loop.name;
  obj["bpm"] = loop.bpm;
  obj["bars"] = loop.bars;
  obj["beatsPerBar"] = loop.beatsPerBar;
  obj["beatValue"] = loop.beatValue;
  obj["ppq"] = loop.ppq;

  JsonArray events = obj["events"].to<JsonArray>();
  for (uint16_t i = 0; i < loop.eventCount; i++) {
    JsonArray evt = events.add<JsonArray>();
    evt.add(loop.events[i].tickPosition);
    evt.add(loop.events[i].midiNote);
    evt.add(loop.events[i].velocity);
    evt.add(loop.events[i].channel);
  }
}

bool LoopEngine::loopFromJson(const JsonObject& obj, Loop& loop) {
  loop.id = obj["id"] | 0;
  strlcpy(loop.name, obj["name"] | "Loop", sizeof(loop.name));
  loop.bpm = obj["bpm"] | MIDI_BPM_DEFAULT;
  loop.bars = obj["bars"] | 1;
  loop.beatsPerBar = obj["beatsPerBar"] | 4;
  loop.beatValue = obj["beatValue"] | 4;
  loop.ppq = obj["ppq"] | 96;
  loop.active = true;

  loop.eventCount = 0;
  JsonArray events = obj["events"].as<JsonArray>();
  if (!events.isNull()) {
    for (JsonArray evt : events) {
      if (loop.eventCount >= MAX_LOOP_EVENTS) break;
      loop.events[loop.eventCount] = {
        (uint16_t)(evt[0] | 0),
        (uint8_t)(evt[1] | 36),
        (uint8_t)(evt[2] | 80),
        (uint8_t)(evt[3] | 10)
      };
      loop.eventCount++;
    }
  }
  _sortEvents(loop);
  return true;
}

// --- Info ---

uint16_t LoopEngine::getCurrentBeat() const {
  if (!_playing || _currentLoop >= _loopCount) return 0;
  const Loop& loop = _loops[_currentLoop];
  return (_currentTick / loop.ppq) % loop.beatsPerBar;
}

uint16_t LoopEngine::getCurrentBar() const {
  if (!_playing || _currentLoop >= _loopCount) return 0;
  const Loop& loop = _loops[_currentLoop];
  return _currentTick / (loop.ppq * loop.beatsPerBar);
}

uint16_t LoopEngine::getBpm() const {
  if (_currentLoop >= _loopCount) return MIDI_BPM_DEFAULT;
  return _loops[_currentLoop].bpm;
}

// --- Private ---

void LoopEngine::_calcTickInterval(uint16_t bpm, uint16_t ppq) {
  _tickIntervalUs = 60000000UL / ((unsigned long)bpm * ppq);
}

uint16_t LoopEngine::_totalTicks(const Loop& loop) const {
  return loop.ppq * loop.beatsPerBar * loop.bars;
}

void LoopEngine::_sortEvents(Loop& loop) {
  for (uint16_t i = 1; i < loop.eventCount; i++) {
    LoopEvent key = loop.events[i];
    int16_t j = i - 1;
    while (j >= 0 && loop.events[j].tickPosition > key.tickPosition) {
      loop.events[j + 1] = loop.events[j];
      j--;
    }
    loop.events[j + 1] = key;
  }
}
