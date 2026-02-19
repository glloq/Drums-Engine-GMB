#ifndef MIDI_ENGINE_H
#define MIDI_ENGINE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <AppleMIDI.h>
#include "../core/config.h"
#include "../core/types.h"
#include "../event/event_processor.h"
#include "../instrument/instrument_manager.h"

// ============================================================================
// MidiEngine - Couche MIDI (reception WiFi rtpMIDI)
// ============================================================================
// Recoit les evenements MIDI, les horodate en microsecondes,
// et les dispatche vers l'EventProcessor (pipeline compile)
// ou directement vers l'InstrumentManager (mode legacy).
// ============================================================================

class MidiEngine {
public:
  MidiEngine(EventProcessor* eventProc, InstrumentManager* instrumentMgr);

  bool begin();
  void update();

  // Envoi MIDI (pour loop playback)
  void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
  void sendNoteOff(uint8_t channel, uint8_t note);

  // Status
  bool isConnected() const { return _connected; }
  uint8_t getSessionCount() const { return _sessionCount; }
  unsigned long getLastActivity() const { return _lastActivity; }

  // MIDI channel filter (bitmask: bit 0 = ch1, bit 9 = ch10, 0xFFFF = all)
  uint16_t getChannelMask() const { return _channelMask; }
  void setChannelMask(uint16_t mask) { _channelMask = mask; }
  bool isChannelAllowed(uint8_t channel) const {
    if (channel < 1 || channel > 16) return false;
    return (_channelMask >> (channel - 1)) & 1;
  }

  // Stats
  uint32_t getNotesReceived() const { return _notesReceived; }
  uint32_t getNotesSent() const { return _notesSent; }
  void resetStats();

private:
  EventProcessor* _eventProc;
  InstrumentManager* _instrumentMgr;
  bool _connected;
  uint8_t _sessionCount;
  unsigned long _lastActivity;
  uint32_t _notesReceived;
  uint32_t _notesSent;
  uint16_t _channelMask = 0xFFFF;  // Default: all channels allowed

  // Use pipeline mode if pipelines are compiled
  bool _usePipelineMode() const;

  // Callbacks statiques (requis par la lib AppleMIDI)
  static MidiEngine* _instance;
  static void _onConnected(const APPLEMIDI_NAMESPACE::ssrc_t& ssrc, const char* name);
  static void _onDisconnected(const APPLEMIDI_NAMESPACE::ssrc_t& ssrc);
  static void _onNoteOn(byte channel, byte note, byte velocity);
  static void _onNoteOff(byte channel, byte note, byte velocity);
  static void _onControlChange(byte channel, byte number, byte value);
  static void _onPitchBend(byte channel, int value);
  static void _onAftertouch(byte channel, byte pressure);
};

#endif // MIDI_ENGINE_H
