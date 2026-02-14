#ifndef LOOP_ENGINE_H
#define LOOP_ENGINE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../core/config.h"
#include "../core/types.h"
#include "../instrument/instrument_manager.h"

// ============================================================================
// LoopEngine - Sequenceur de boucles
// ============================================================================

class LoopEngine {
public:
  LoopEngine(InstrumentManager* instrumentMgr);

  // Controle de lecture
  void play(uint8_t loopIndex);
  void stop();
  void pause();
  void resume();
  bool isPlaying() const { return _playing; }
  bool isPaused() const { return _paused; }

  // Appeler dans loop()
  void update();

  // Gestion des boucles
  int createLoop(const char* name, uint16_t bpm, uint8_t bars,
                 uint8_t beatsPerBar = 4, uint8_t beatValue = 4);
  bool deleteLoop(uint8_t index);
  Loop* getLoop(uint8_t index);
  uint8_t getLoopCount() const { return _loopCount; }

  // Edition d'evenements
  int addEvent(uint8_t loopIndex, uint16_t tick, uint8_t note,
               uint8_t velocity, uint8_t channel = 10);
  bool removeEvent(uint8_t loopIndex, uint16_t eventIndex);
  bool clearEvents(uint8_t loopIndex);

  // Quantization
  void quantize(uint8_t loopIndex, uint8_t gridDivision);

  // Serialization
  void loopToJson(const Loop& loop, JsonObject& obj) const;
  bool loopFromJson(const JsonObject& obj, Loop& loop);

  // Info de lecture
  uint8_t getCurrentLoopIndex() const { return _currentLoop; }
  uint16_t getCurrentTick() const { return _currentTick; }
  uint16_t getCurrentBeat() const;
  uint16_t getCurrentBar() const;
  uint16_t getBpm() const;

private:
  InstrumentManager* _instrumentMgr;
  Loop _loops[MAX_LOOPS];
  uint8_t _loopCount;

  bool _playing;
  bool _paused;
  uint8_t _currentLoop;
  uint16_t _currentTick;
  uint16_t _nextEventIndex;
  unsigned long _lastTickTime;
  unsigned long _tickIntervalUs;

  void _calcTickInterval(uint16_t bpm, uint16_t ppq);
  uint16_t _totalTicks(const Loop& loop) const;
  void _sortEvents(Loop& loop);
};

#endif // LOOP_ENGINE_H
