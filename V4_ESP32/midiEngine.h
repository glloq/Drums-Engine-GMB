#ifndef MIDI_ENGINE_H
#define MIDI_ENGINE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <AppleMIDI.h>
#include "config.h"
#include "instrumentManager.h"

// ============================================================================
// WiFi MIDI Engine - AppleMIDI (rtpMIDI) for ESP32
// Replaces USB MIDI from V3
// ============================================================================

class MidiEngine {
public:
  MidiEngine(InstrumentManager* instrumentMgr);

  // Initialize MIDI over WiFi
  bool begin();

  // Must be called in loop()
  void update();

  // Send MIDI messages (for loop playback)
  void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
  void sendNoteOff(uint8_t channel, uint8_t note);

  // Status
  bool isConnected() const { return _connected; }
  uint8_t getSessionCount() const { return _sessionCount; }
  unsigned long getLastActivity() const { return _lastActivity; }

  // Stats
  uint32_t getNotesReceived() const { return _notesReceived; }
  uint32_t getNotesSent() const { return _notesSent; }
  void resetStats();

private:
  InstrumentManager* _instrumentMgr;
  bool _connected;
  uint8_t _sessionCount;
  unsigned long _lastActivity;
  uint32_t _notesReceived;
  uint32_t _notesSent;

  // Callbacks
  static MidiEngine* _instance;  // For static callback routing
  static void _onConnected(const APPLEMIDI_NAMESPACE::ssrc_t& ssrc, const char* name);
  static void _onDisconnected(const APPLEMIDI_NAMESPACE::ssrc_t& ssrc);
  static void _onNoteOn(byte channel, byte note, byte velocity);
  static void _onNoteOff(byte channel, byte note, byte velocity);
  static void _onControlChange(byte channel, byte number, byte value);
  static void _onPitchBend(byte channel, int value);
  static void _onAftertouch(byte channel, byte pressure);
};

#endif // MIDI_ENGINE_H
