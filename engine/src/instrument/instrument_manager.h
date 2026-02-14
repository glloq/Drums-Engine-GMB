#ifndef INSTRUMENT_MANAGER_H
#define INSTRUMENT_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../core/config.h"
#include "../core/types.h"
#include "../actuator/actuator_manager.h"
#include "../scheduler/scheduler.h"

// ============================================================================
// InstrumentManager - Gestion CRUD des instruments + dispatch MIDI legacy
// ============================================================================
// Mode legacy (sans pipelines compiles) : les instruments dispatched
// directement les notes MIDI vers les actionneurs.
// Mode pipeline : l'EventProcessor prend le relais.
// ============================================================================

class InstrumentManager {
public:
  InstrumentManager(ActuatorManager* actMgr, Scheduler* scheduler);

  // --- CRUD Instruments ---
  int addInstrument(const InstrumentConfig& cfg);
  bool updateInstrument(uint8_t id, const InstrumentConfig& cfg);
  bool removeInstrument(uint8_t id);
  InstrumentConfig* getInstrument(uint8_t id);
  InstrumentConfig* getInstrumentByIndex(uint8_t index);
  uint8_t getInstrumentCount() const { return _count; }

  // --- MIDI dispatch (mode legacy, sans pipeline) ---
  void onNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
  void onNoteOff(uint8_t channel, uint8_t note);
  void onControlChange(uint8_t channel, uint8_t cc, uint8_t value);
  void onPitchBend(uint8_t channel, int16_t value);
  void onAftertouch(uint8_t channel, uint8_t pressure);

  // --- Testing ---
  void testInstrument(uint8_t id, uint8_t velocity = 80);

  // --- Serialization ---
  void toJson(JsonArray& arr) const;
  bool fromJson(const JsonArray& arr);
  void instrumentToJson(const InstrumentConfig& inst, JsonObject& obj) const;
  bool instrumentFromJson(const JsonObject& obj, InstrumentConfig& inst);

  // --- Lookup ---
  InstrumentConfig* findByMidiNote(uint8_t note);
  uint8_t nextId() const;

private:
  ActuatorManager* _actMgr;
  Scheduler* _scheduler;

  InstrumentConfig _instruments[MAX_INSTRUMENTS];
  uint8_t _count;

  // Lookup O(1) : note MIDI -> index instrument
  int8_t _noteMap[128];

  void _rebuildNoteMap();
};

#endif // INSTRUMENT_MANAGER_H
