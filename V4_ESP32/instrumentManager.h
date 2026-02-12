#ifndef INSTRUMENT_MANAGER_H
#define INSTRUMENT_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "actuatorTypes.h"
#include "actuatorDriver.h"
#include "scheduler.h"

// ============================================================================
// Instrument CRUD manager + MIDI dispatch
// ============================================================================

class InstrumentManager {
public:
  InstrumentManager(ActuatorDriver* driver, Scheduler* scheduler);

  // --- CRUD Instruments ---
  int addInstrument(const InstrumentConfig& cfg);         // Returns index or -1
  bool updateInstrument(uint8_t id, const InstrumentConfig& cfg);
  bool removeInstrument(uint8_t id);
  InstrumentConfig* getInstrument(uint8_t id);
  InstrumentConfig* getInstrumentByIndex(uint8_t index);
  uint8_t getInstrumentCount() const { return _count; }

  // --- CRUD Actuators ---
  int addActuator(uint8_t instrumentId, const ActuatorConfig& act);   // Returns act index or -1
  bool updateActuator(uint8_t instrumentId, uint8_t actIndex, const ActuatorConfig& act);
  bool removeActuator(uint8_t instrumentId, uint8_t actIndex);

  // --- MIDI dispatch ---
  void onNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
  void onNoteOff(uint8_t channel, uint8_t note);
  void onControlChange(uint8_t channel, uint8_t cc, uint8_t value);
  void onPitchBend(uint8_t channel, int16_t value);
  void onAftertouch(uint8_t channel, uint8_t pressure);

  // --- Testing ---
  void testInstrument(uint8_t id, uint8_t velocity = 80);
  void testActuator(uint8_t instrumentId, uint8_t actIndex, uint8_t value = 80);

  // --- Serialization ---
  void toJson(JsonArray& arr) const;
  bool fromJson(const JsonArray& arr);
  void instrumentToJson(const InstrumentConfig& inst, JsonObject& obj) const;
  bool instrumentFromJson(const JsonObject& obj, InstrumentConfig& inst);

  // --- Lookup ---
  // Find instrument by MIDI note (for fast dispatch)
  InstrumentConfig* findByMidiNote(uint8_t note);

  // Generate next unique ID
  uint8_t nextId() const;

private:
  ActuatorDriver* _driver;
  Scheduler* _scheduler;

  InstrumentConfig _instruments[MAX_INSTRUMENTS];
  uint8_t _count;

  // Fast MIDI note lookup (128 entries, index = MIDI note)
  int8_t _noteMap[128];   // -1 = no instrument, 0-15 = instrument index

  void _rebuildNoteMap();
  void _triggerActuators(InstrumentConfig& inst, ActuatorTiming timing,
                         uint8_t value, bool isNoteOff = false);
};

#endif // INSTRUMENT_MANAGER_H
