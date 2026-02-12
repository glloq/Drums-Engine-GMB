#ifndef LOOP_ENGINE_H
#define LOOP_ENGINE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "instrumentManager.h"

// ============================================================================
// Loop sequencer - record, store, and play back drum loops
// ============================================================================

// A single event in a loop
struct LoopEvent {
  uint16_t tickPosition;    // Position in ticks (PPQ-based)
  uint8_t midiNote;         // MIDI note
  uint8_t velocity;         // Velocity (0 = note off)
  uint8_t channel;          // MIDI channel
};

// A complete loop
struct Loop {
  uint8_t id;
  char name[32];
  uint16_t bpm;
  uint8_t bars;             // Number of bars (measures)
  uint8_t beatsPerBar;      // Time signature numerator
  uint8_t beatValue;        // Time signature denominator (4 = quarter)
  uint16_t ppq;             // Pulses per quarter note (resolution)
  uint16_t eventCount;
  LoopEvent events[MAX_LOOP_EVENTS];
  bool active;
};

class LoopEngine {
public:
  LoopEngine(InstrumentManager* instrumentMgr);

  // Playback control
  void play(uint8_t loopIndex);
  void stop();
  void pause();
  void resume();
  bool isPlaying() const { return _playing; }
  bool isPaused() const { return _paused; }

  // Must be called in loop() for playback timing
  void update();

  // Loop management
  int createLoop(const char* name, uint16_t bpm, uint8_t bars,
                 uint8_t beatsPerBar = 4, uint8_t beatValue = 4);
  bool deleteLoop(uint8_t index);
  Loop* getLoop(uint8_t index);
  uint8_t getLoopCount() const { return _loopCount; }

  // Event editing
  int addEvent(uint8_t loopIndex, uint16_t tick, uint8_t note,
               uint8_t velocity, uint8_t channel = 10);
  bool removeEvent(uint8_t loopIndex, uint16_t eventIndex);
  bool clearEvents(uint8_t loopIndex);

  // Quantize events to grid
  void quantize(uint8_t loopIndex, uint8_t gridDivision); // 4=quarter, 8=eighth, 16=sixteenth

  // Serialization
  void loopToJson(const Loop& loop, JsonObject& obj) const;
  bool loopFromJson(const JsonObject& obj, Loop& loop);

  // Playback info
  uint8_t getCurrentLoopIndex() const { return _currentLoop; }
  uint16_t getCurrentTick() const { return _currentTick; }
  uint16_t getCurrentBeat() const;
  uint16_t getCurrentBar() const;
  uint16_t getBpm() const;

private:
  InstrumentManager* _instrumentMgr;
  Loop _loops[MAX_LOOPS];
  uint8_t _loopCount;

  // Playback state
  bool _playing;
  bool _paused;
  uint8_t _currentLoop;
  uint16_t _currentTick;
  uint16_t _nextEventIndex;
  unsigned long _lastTickTime;
  unsigned long _tickIntervalUs;  // Microseconds per tick

  void _calcTickInterval(uint16_t bpm, uint16_t ppq);
  uint16_t _totalTicks(const Loop& loop) const;
  void _sortEvents(Loop& loop);
};

#endif // LOOP_ENGINE_H
