#ifndef MIDI_ENGINE_H
#define MIDI_ENGINE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <AppleMIDI.h>
#include <atomic>
#include "../core/config.h"
#include "../core/types.h"
#include "../event/event_processor.h"
#include "../instrument/instrument_manager.h"
#include "../gmb/gmb_sysex_service.h"

// ============================================================================
// MidiEngine - Couche MIDI (reception WiFi rtpMIDI)
// ============================================================================
// Recoit les evenements MIDI, les horodate en microsecondes,
// et les dispatche vers l'EventProcessor (pipeline compile)
// ou directement vers l'InstrumentManager (mode legacy).
//
// SysEx / General-Midi-Boop
// -------------------------
// rtpMIDI est bidirectionnel : c'est ce qui rend la reconnaissance automatique
// possible (SYSEX_IDENTITY.md §8 — sans voie de retour, aucune decouverte).
// C'est aujourd'hui le SEUL transport du moteur, donc le seul a annoncer le
// SysEx GMB. Le traitement se limite a analyser 6 a 8 octets et a recopier un
// segment deja calcule : il tient dans le contexte de reception MIDI du coeur
// temps reel sans rien changer au chemin NoteOn/NoteOff/CC, qui ne passe meme
// pas par la.
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

  // --- GMB (reconnaissance automatique) ---
  // Sans service branche, aucun SysEx n'est ni analyse ni emis.
  void setGmbService(GmbSysExService* svc) { _gmb = svc; }
  // Emettre un message SysEx complet (F0..F7 inclus).
  void sendSysEx(const uint8_t* data, size_t len);
  uint32_t getSysExSent() const { return _sysexSent.load(std::memory_order_relaxed); }

  // MIDI channel filter (bitmask: bit 0 = ch1, bit 9 = ch10, 0xFFFF = all)
  uint16_t getChannelMask() const { return _channelMask; }
  void setChannelMask(uint16_t mask) { _channelMask = mask; }
  bool isChannelAllowed(uint8_t channel) const {
    if (channel < 1 || channel > 16) return false;
    return (_channelMask >> (channel - 1)) & 1;
  }

  // Stats (atomic: accessed from both Core 0 and Core 1 callbacks)
  uint32_t getNotesReceived() const { return _notesReceived.load(std::memory_order_relaxed); }
  uint32_t getNotesSent() const { return _notesSent.load(std::memory_order_relaxed); }
  void resetStats();

private:
  EventProcessor* _eventProc;
  InstrumentManager* _instrumentMgr;
  volatile bool _connected;
  volatile uint8_t _sessionCount;
  volatile unsigned long _lastActivity;
  std::atomic<uint32_t> _notesReceived;
  std::atomic<uint32_t> _notesSent;
  std::atomic<uint32_t> _sysexSent{0};
  uint16_t _channelMask = 0xFFFF;  // Default: all channels allowed
  GmbSysExService* _gmb = nullptr;

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
  static void _onSysEx(byte* data, unsigned size);
};

#endif // MIDI_ENGINE_H
