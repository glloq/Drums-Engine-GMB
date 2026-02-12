#include "midiEngine.h"

// AppleMIDI global instance (required by library)
USING_NAMESPACE_APPLEMIDI
APPLEMIDI_CREATE_DEFAULTSESSION_INSTANCE();

MidiEngine* MidiEngine::_instance = nullptr;

MidiEngine::MidiEngine(InstrumentManager* instrumentMgr)
  : _instrumentMgr(instrumentMgr), _connected(false), _sessionCount(0),
    _lastActivity(0), _notesReceived(0), _notesSent(0) {
  _instance = this;
}

bool MidiEngine::begin() {
  DBGLN("[MIDI] Starting AppleMIDI (rtpMIDI)...");

  // Initialize AppleMIDI session
  MIDI.begin(MIDI_CHANNEL_OMNI);  // Listen on all channels, filter in dispatch

  // Register callbacks
  AppleMIDI.setHandleConnected(_onConnected);
  AppleMIDI.setHandleDisconnected(_onDisconnected);

  MIDI.setHandleNoteOn(_onNoteOn);
  MIDI.setHandleNoteOff(_onNoteOff);
  MIDI.setHandleControlChange(_onControlChange);
  MIDI.setHandlePitchBend(_onPitchBend);
  MIDI.setHandleAfterTouchChannel(_onAftertouch);

  DBGF("[MIDI] AppleMIDI session '%s' on port %d\n", MIDI_SESSION_NAME, RTP_MIDI_PORT);
  DBGLN("[MIDI] Waiting for connections...");
  return true;
}

void MidiEngine::update() {
  MIDI.read();
}

void MidiEngine::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  MIDI.sendNoteOn(note, velocity, channel);
  _notesSent++;
  _lastActivity = millis();

  // Also dispatch locally to instruments
  _instrumentMgr->onNoteOn(channel, note, velocity);
}

void MidiEngine::sendNoteOff(uint8_t channel, uint8_t note) {
  MIDI.sendNoteOff(note, 0, channel);
  _notesSent++;
  _lastActivity = millis();

  _instrumentMgr->onNoteOff(channel, note);
}

void MidiEngine::resetStats() {
  _notesReceived = 0;
  _notesSent = 0;
}

// --- Static callbacks ---

void MidiEngine::_onConnected(const ssrc_t& ssrc, const char* name) {
  if (!_instance) return;
  _instance->_connected = true;
  _instance->_sessionCount++;
  DBGF("[MIDI] Connected: %s (ssrc=%u)\n", name, ssrc);
}

void MidiEngine::_onDisconnected(const ssrc_t& ssrc) {
  if (!_instance) return;
  if (_instance->_sessionCount > 0) _instance->_sessionCount--;
  _instance->_connected = (_instance->_sessionCount > 0);
  DBGF("[MIDI] Disconnected (ssrc=%u), sessions=%d\n", ssrc, _instance->_sessionCount);
}

void MidiEngine::_onNoteOn(byte channel, byte note, byte velocity) {
  if (!_instance) return;
  _instance->_notesReceived++;
  _instance->_lastActivity = millis();
  _instance->_instrumentMgr->onNoteOn(channel, note, velocity);
}

void MidiEngine::_onNoteOff(byte channel, byte note, byte velocity) {
  if (!_instance) return;
  _instance->_notesReceived++;
  _instance->_lastActivity = millis();
  _instance->_instrumentMgr->onNoteOff(channel, note);
}

void MidiEngine::_onControlChange(byte channel, byte number, byte value) {
  if (!_instance) return;
  _instance->_lastActivity = millis();
  _instance->_instrumentMgr->onControlChange(channel, number, value);
}

void MidiEngine::_onPitchBend(byte channel, int value) {
  if (!_instance) return;
  _instance->_lastActivity = millis();
  _instance->_instrumentMgr->onPitchBend(channel, (int16_t)value);
}

void MidiEngine::_onAftertouch(byte channel, byte pressure) {
  if (!_instance) return;
  _instance->_lastActivity = millis();
  _instance->_instrumentMgr->onAftertouch(channel, pressure);
}
