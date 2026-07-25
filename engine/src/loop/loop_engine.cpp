#include "loop_engine.h"
#include "../event/event_processor.h"
#include "../core/panic_state.h"

LoopEngine::LoopEngine(InstrumentManager* instrumentMgr)
  : _instrumentMgr(instrumentMgr), _eventProc(nullptr), _loopCount(0),
    _playing(false), _paused(false), _currentLoop(0),
    _currentTick(0), _nextEventIndex(0), _lastTickTime(0),
    _tickIntervalUs(0), _tickRemainderPerTick(0), _tickRemainderAccum(0),
    _chainActive(false), _chainRepeat(false), _chainLength(0), _chainIndex(0) {}

// --- Lecture ---

bool LoopEngine::play(uint8_t loopIndex) {
  if (g_panicActive.load(std::memory_order_acquire)) return false;  // P0 #3
  if (loopIndex >= _loopCount) return false;
  Loop& loop = _loops[loopIndex];
  if (loop.eventCount == 0) return false;

  _currentLoop = loopIndex;
  _currentTick = 0;
  _nextEventIndex = 0;
  _playing = true;
  _paused = false;
  _lastTickTime = micros();
  _calcTickInterval(loop.bpm, loop.ppq);

  // P1 #11: fire events at tick 0 now — update() increments _currentTick before
  // its dispatch check, so without this the first tick's events are skipped.
  _fireEventsAtCurrentTick(loop);

  DBGF("[Loop] Playing '%s' at %d BPM (%d events)\n",
       loop.name, loop.bpm, loop.eventCount);
  return true;
}

bool LoopEngine::playChain(const uint8_t* loopIds, uint8_t count, bool repeat) {
  if (g_panicActive.load(std::memory_order_acquire)) return false;  // P0 #3
  if (count == 0) return false;
  _chainLength = (count > MAX_LOOPS) ? MAX_LOOPS : count;
  for (uint8_t i = 0; i < _chainLength; i++) _chain[i] = loopIds[i];
  _chainRepeat = repeat;
  _chainIndex = 0;
  _chainActive = true;
  bool started = play(_chain[0]);
  if (!started) { _chainActive = false; return false; }
  DBGF("[Loop] Chain started (%d loops, repeat=%d)\n", _chainLength, repeat);
  return true;
}

void LoopEngine::stop() {
  _playing = false;
  _paused = false;
  _currentTick = 0;
  _nextEventIndex = 0;
  _chainActive = false;
  _chainIndex = 0;
  _chainLength = 0;
  DBGLN("[Loop] Stopped");
}

void LoopEngine::pause() {
  if (_playing) { _paused = true; DBGLN("[Loop] Paused"); }
}

void LoopEngine::resume() {
  if (_paused) { _paused = false; _lastTickTime = micros(); DBGLN("[Loop] Resumed"); }
}

void LoopEngine::update() {
  if (g_panicActive.load(std::memory_order_acquire)) return;  // P0 #3
  if (!_playing || _paused) return;
  if (_currentLoop >= _loopCount) { stop(); return; }

  Loop& loop = _loops[_currentLoop];
  unsigned long now = micros();
  unsigned long elapsed = now - _lastTickTime;

  while (elapsed >= _tickIntervalUs) {
    elapsed -= _tickIntervalUs;
    // Fix #15: Fractional accumulator to prevent timing drift
    _tickRemainderAccum += _tickRemainderPerTick;
    unsigned long extra = _tickRemainderAccum / 1000;
    _tickRemainderAccum -= extra * 1000;
    _lastTickTime += _tickIntervalUs + extra;
    _currentTick++;

    uint16_t total = _totalTicks(loop);
    if (_currentTick >= total) {
      if (_chainActive) {
        _advanceChain();
        if (!_playing) return;  // Chain finished, stop
        break;  // New loop started, recalc timing
      }
      _currentTick = 0;
      _nextEventIndex = 0;
    }

    _fireEventsAtCurrentTick(loop);
  }
}

void LoopEngine::_fireEventsAtCurrentTick(Loop& loop) {
  while (_nextEventIndex < loop.eventCount &&
         loop.events[_nextEventIndex].tickPosition == _currentTick) {
    _dispatchEvent(loop.events[_nextEventIndex]);
    _nextEventIndex++;
  }
}

void LoopEngine::_dispatchEvent(const LoopEvent& evt) {
  // Route par EventProcessor (pipeline) si disponible et pipelines compiles
  if (_eventProc && _eventProc->getLookup().pipeline_count > 0) {
    MidiEvent midiEv;
    midiEv.channel = evt.channel;
    midiEv.data1 = evt.midiNote;
    midiEv.data2 = evt.velocity;
    midiEv.timestamp = micros();

    if (evt.velocity > 0) {
      midiEv.type = MIDI_EVT_NOTE_ON;
    } else {
      midiEv.type = MIDI_EVT_NOTE_OFF;
    }

    _eventProc->processMidiEvent(midiEv);
  } else {
    // Fallback legacy: route par InstrumentManager
    if (evt.velocity > 0) {
      _instrumentMgr->onNoteOn(evt.channel, evt.midiNote, evt.velocity);
    } else {
      _instrumentMgr->onNoteOff(evt.channel, evt.midiNote);
    }
  }
}

// --- Gestion ---

bool LoopEngine::validateLoopParams(uint16_t bpm, uint8_t bars, uint8_t beatsPerBar,
                                    uint8_t beatValue, uint16_t ppq, const char*& errMsg) {
  if (bpm < 20 || bpm > 400)          { errMsg = "BPM out of range (20-400)"; return false; }
  if (bars < 1 || bars > 64)          { errMsg = "bars out of range (1-64)"; return false; }
  if (beatsPerBar < 1 || beatsPerBar > 16) { errMsg = "beatsPerBar out of range (1-16)"; return false; }
  if (!(beatValue == 1 || beatValue == 2 || beatValue == 4 ||
        beatValue == 8 || beatValue == 16)) { errMsg = "beatValue must be 1,2,4,8 or 16"; return false; }
  if (ppq < 24 || ppq > 960)          { errMsg = "PPQ out of range (24-960)"; return false; }
  // Guard against uint16_t overflow of the total tick count (same beatValue-aware
  // formula as _totalTicks). beatValue is already validated non-zero above.
  uint32_t total = (uint32_t)ppq * 4 * beatsPerBar * bars / beatValue;
  if (total == 0 || total > 65535)    { errMsg = "loop too long (total ticks must be <= 65535)"; return false; }
  errMsg = nullptr;
  return true;
}

int LoopEngine::createLoop(const char* name, uint16_t bpm, uint8_t bars,
                            uint8_t beatsPerBar, uint8_t beatValue) {
  if (_loopCount >= MAX_LOOPS) return -1;

  // P1 #12: reject out-of-range time signatures (also guards _calcTickInterval
  // against division by zero and _totalTicks against uint16_t overflow).
  const char* err = nullptr;
  if (!validateLoopParams(bpm, bars, beatsPerBar, beatValue, 96, err)) {
    DBGF("[Loop] Rejected invalid params: %s\n", err ? err : "?");
    return -1;
  }

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

  // C5 fix: Adjust chain indices BEFORE potential stop(), since stop() clears state
  if (_chainActive) {
    bool shouldStop = false;
    for (uint8_t i = 0; i < _chainLength; i++) {
      if (_chain[i] == index) {
        shouldStop = true;
        break;
      }
    }
    // Adjust remaining chain references: indices above deleted one shift down
    for (uint8_t i = 0; i < _chainLength; i++) {
      if (_chain[i] > index) _chain[i]--;
    }
    if (shouldStop) {
      DBGLN("[Loop] Chain stopped (deleted loop was referenced)");
      stop();
    }
  }

  if (_playing && _currentLoop == index) stop();

  for (uint8_t i = index; i < _loopCount - 1; i++) {
    _loops[i] = _loops[i + 1];
    _loops[i].id = i;
  }
  _loopCount--;

  // Fix #20: Adjust _currentLoop index after delete
  if (_currentLoop >= _loopCount && _loopCount > 0) {
    _currentLoop = _loopCount - 1;
  }
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

  // review #7: addEvent is the common insertion point — validate/clamp here so
  // the create route (which passes JSON events straight through) can't inject
  // out-of-range values. (loopFromJson clamps on the load path likewise.)
  uint16_t total = _totalTicks(loop);
  if (total > 0 && tick >= total) return -1;   // event past the loop end
  if (note > 127) note = 127;
  if (velocity > 127) velocity = 127;
  if (channel < 1) channel = 1;
  if (channel > 16) channel = 16;

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
  // Fix #18: Guard against division by zero
  if (gridDivision == 0) return;
  Loop& loop = _loops[loopIndex];

  uint16_t gridTicks = (loop.ppq * 4) / gridDivision;
  if (gridTicks == 0) return;

  uint16_t totalTicks = _totalTicks(loop);
  for (uint16_t i = 0; i < loop.eventCount; i++) {
    uint16_t tick = loop.events[i].tickPosition;
    uint16_t remainder = tick % gridTicks;
    if (remainder > gridTicks / 2) {
      tick = tick + (gridTicks - remainder);
    } else {
      tick = tick - remainder;
    }
    // Fix #17: Clamp to loop boundaries
    if (tick >= totalTicks) tick = (totalTicks > 0) ? totalTicks - 1 : 0;
    loop.events[i].tickPosition = tick;
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

  // P1 #12 / #14: a hand-edited or corrupted file must not be able to inject
  // out-of-range time signatures (div-by-zero, tick overflow). Fall back to
  // safe defaults if the stored parameters don't validate.
  const char* err = nullptr;
  if (!validateLoopParams(loop.bpm, loop.bars, loop.beatsPerBar,
                          loop.beatValue, loop.ppq, err)) {
    DBGF("[Loop] Loaded loop had invalid params (%s), using defaults\n",
         err ? err : "?");
    loop.bpm = MIDI_BPM_DEFAULT;
    loop.bars = 1;
    loop.beatsPerBar = 4;
    loop.beatValue = 4;
    loop.ppq = 96;
  }

  uint16_t total = _totalTicks(loop);
  loop.eventCount = 0;
  JsonArray events = obj["events"].as<JsonArray>();
  if (!events.isNull()) {
    for (JsonArray evt : events) {
      if (loop.eventCount >= MAX_LOOP_EVENTS) break;
      uint16_t tick = (uint16_t)(evt[0] | 0);
      uint8_t note = (uint8_t)(evt[1] | 36);
      uint8_t vel  = (uint8_t)(evt[2] | 80);
      uint8_t chan = (uint8_t)(evt[3] | 10);
      // Clamp event fields into valid ranges (P1 #12).
      if (tick >= total) continue;      // drop events past the loop end
      if (note > 127) note = 127;
      if (vel > 127) vel = 127;
      if (chan < 1) chan = 1;
      if (chan > 16) chan = 16;
      loop.events[loop.eventCount] = { tick, note, vel, chan };
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
  uint8_t bv = loop.beatValue ? loop.beatValue : 4;
  uint32_t ticksPerBeat = (uint32_t)loop.ppq * 4 / bv;   // review #16
  if (ticksPerBeat == 0) return 0;
  return (uint16_t)((_currentTick / ticksPerBeat) % loop.beatsPerBar);
}

uint16_t LoopEngine::getCurrentBar() const {
  if (!_playing || _currentLoop >= _loopCount) return 0;
  const Loop& loop = _loops[_currentLoop];
  uint8_t bv = loop.beatValue ? loop.beatValue : 4;
  uint32_t ticksPerBar = (uint32_t)loop.ppq * 4 * loop.beatsPerBar / bv;  // review #16
  if (ticksPerBar == 0) return 0;
  return (uint16_t)(_currentTick / ticksPerBar);
}

uint16_t LoopEngine::getBpm() const {
  if (_currentLoop >= _loopCount) return MIDI_BPM_DEFAULT;
  return _loops[_currentLoop].bpm;
}

// --- Private ---

void LoopEngine::_advanceChain() {
  _chainIndex++;
  if (_chainIndex >= _chainLength) {
    if (_chainRepeat) {
      _chainIndex = 0;
    } else {
      stop();
      return;
    }
  }
  // Start next loop in chain (preserves _chainActive state)
  uint8_t nextLoop = _chain[_chainIndex];
  if (nextLoop >= _loopCount) { stop(); return; }
  Loop& loop = _loops[nextLoop];
  if (loop.eventCount == 0) { stop(); return; }
  _currentLoop = nextLoop;
  _currentTick = 0;
  _nextEventIndex = 0;
  _lastTickTime = micros();
  _calcTickInterval(loop.bpm, loop.ppq);
  _fireEventsAtCurrentTick(loop);  // P1 #11: tick-0 events on chain advance
  DBGF("[Loop] Chain advancing to loop %d ('%s')\n", nextLoop, loop.name);
}

void LoopEngine::_calcTickInterval(uint16_t bpm, uint16_t ppq) {
  unsigned long divisor = (unsigned long)bpm * ppq;
  // P1 #12: defensive guard — never divide by zero even if a bad value slipped
  // through (e.g. a corrupted file loaded before validation).
  if (divisor == 0) {
    _tickIntervalUs = 60000000UL / ((unsigned long)MIDI_BPM_DEFAULT * 96);
    _tickRemainderPerTick = 0;
    _tickRemainderAccum = 0;
    return;
  }
  _tickIntervalUs = 60000000UL / divisor;
  // Fix #15: Store fractional remainder (x1000) to compensate drift
  unsigned long remainderUs = 60000000UL % divisor;
  _tickRemainderPerTick = (remainderUs * 1000UL) / divisor;
  _tickRemainderAccum = 0;
}

uint16_t LoopEngine::_totalTicks(const Loop& loop) const {
  // review #16: honor beatValue. ppq is ticks per QUARTER note, so ticks per
  // whole note = ppq*4, and each beat lasts (whole / beatValue). Total ticks =
  // ppq*4 * beatsPerBar * bars / beatValue. For x/4 signatures this equals the
  // previous ppq*beatsPerBar*bars (backward compatible).
  uint8_t bv = loop.beatValue ? loop.beatValue : 4;   // defensive
  uint32_t total = (uint32_t)loop.ppq * 4 * loop.beatsPerBar * loop.bars / bv;
  return (total > 65535) ? 65535 : (uint16_t)total;
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
