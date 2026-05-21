#include "midi_engine.h"

// AppleMIDI global instance (requis par la librairie)
USING_NAMESPACE_APPLEMIDI
APPLEMIDI_CREATE_DEFAULTSESSION_INSTANCE();

MidiEngine* MidiEngine::_instance = nullptr;

MidiEngine::MidiEngine(EventProcessor* eventProc, InstrumentManager* instrumentMgr)
  : _eventProc(eventProc), _instrumentMgr(instrumentMgr),
    _connected(false), _sessionCount(0),
    _lastActivity(0), _notesReceived(0), _notesSent(0),
    _midiOverflowCount(0) {
  _instance = this;
}

bool MidiEngine::begin() {
  DBGLN("[MIDI] Starting AppleMIDI (rtpMIDI)...");

  MIDI.begin(MIDI_CHANNEL_OMNI);

  AppleMIDI.setHandleConnected(_onConnected);
  AppleMIDI.setHandleDisconnected(_onDisconnected);

  MIDI.setHandleNoteOn(_onNoteOn);
  MIDI.setHandleNoteOff(_onNoteOff);
  MIDI.setHandleControlChange(_onControlChange);
  MIDI.setHandlePitchBend(_onPitchBend);
  MIDI.setHandleAfterTouchChannel(_onAftertouch);

  DBGF("[MIDI] Session '%s' on port %d\n", MIDI_SESSION_NAME, RTP_MIDI_PORT);
  return true;
}

void MidiEngine::update() {
  MIDI.read();
}

void MidiEngine::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  MIDI.sendNoteOn(note, velocity, channel);
  _notesSent.fetch_add(1, std::memory_order_relaxed);
  _lastActivity.store(millis(), std::memory_order_relaxed);

  // Dispatch local
  if (_usePipelineMode()) {
    MidiEvent ev;
    ev.type = MIDI_EVT_NOTE_ON;
    ev.channel = channel;
    ev.data1 = note;
    ev.data2 = velocity;
    ev.timestamp = micros();
    _eventProc->processMidiEvent(ev);
  } else {
    _instrumentMgr->onNoteOn(channel, note, velocity);
  }
}

void MidiEngine::sendNoteOff(uint8_t channel, uint8_t note) {
  MIDI.sendNoteOff(note, 0, channel);
  _notesSent.fetch_add(1, std::memory_order_relaxed);
  _lastActivity.store(millis(), std::memory_order_relaxed);

  if (_usePipelineMode()) {
    MidiEvent ev;
    ev.type = MIDI_EVT_NOTE_OFF;
    ev.channel = channel;
    ev.data1 = note;
    ev.data2 = 0;
    ev.timestamp = micros();
    _eventProc->processMidiEvent(ev);
  } else {
    _instrumentMgr->onNoteOff(channel, note);
  }
}

void MidiEngine::resetStats() {
  _notesReceived.store(0, std::memory_order_relaxed);
  _notesSent.store(0, std::memory_order_relaxed);
  _midiOverflowCount.store(0, std::memory_order_relaxed);
}

void MidiEngine::ingestExternalMidi(const MidiEvent& ev) {
  // Common entry point for non-rtpMIDI transports (UART, USB, BLE...).
  // Applies channel filter, accounting and dispatch identically to AppleMIDI callbacks.
  if (ev.type == MIDI_EVT_NOTE_ON || ev.type == MIDI_EVT_NOTE_OFF) {
    _notesReceived.fetch_add(1, std::memory_order_relaxed);
  }
  _lastActivity.store(millis(), std::memory_order_relaxed);

  if (!isChannelAllowed(ev.channel)) return;

  // NoteOn vel=0 → NoteOff (MIDI spec)
  MidiEvent evNorm = ev;
  if (evNorm.type == MIDI_EVT_NOTE_ON && evNorm.data2 == 0) {
    evNorm.type = MIDI_EVT_NOTE_OFF;
    evNorm.data2 = 64;
  }

  if (_usePipelineMode()) {
    _eventProc->processMidiEvent(evNorm);
  } else if (_instrumentMgr) {
    switch (evNorm.type) {
      case MIDI_EVT_NOTE_ON:  _instrumentMgr->onNoteOn(evNorm.channel, evNorm.data1, evNorm.data2); break;
      case MIDI_EVT_NOTE_OFF: _instrumentMgr->onNoteOff(evNorm.channel, evNorm.data1); break;
      case MIDI_EVT_CC:       _instrumentMgr->onControlChange(evNorm.channel, evNorm.data1, evNorm.data2); break;
      case MIDI_EVT_PITCH_BEND: {
        int16_t bend = ((int16_t)evNorm.data1 << 7) | (int16_t)evNorm.data2;
        _instrumentMgr->onPitchBend(evNorm.channel, bend);
        break;
      }
      case MIDI_EVT_AFTERTOUCH: _instrumentMgr->onAftertouch(evNorm.channel, evNorm.data1); break;
      default: break;
    }
  }
}

bool MidiEngine::_usePipelineMode() const {
  return _eventProc && _eventProc->getLookup().pipeline_count > 0;
}

// --- Callbacks statiques ---

void MidiEngine::_onConnected(const ssrc_t& ssrc, const char* name) {
  if (!_instance) return;
  _instance->_connected.store(true, std::memory_order_relaxed);
  _instance->_sessionCount.fetch_add(1, std::memory_order_relaxed);
  DBGF("[MIDI] Connected: %s (ssrc=%u)\n", name, ssrc);
}

void MidiEngine::_onDisconnected(const ssrc_t& ssrc) {
  if (!_instance) return;
  uint8_t prev = _instance->_sessionCount.load(std::memory_order_relaxed);
  if (prev > 0) _instance->_sessionCount.fetch_sub(1, std::memory_order_relaxed);
  uint8_t newCount = _instance->_sessionCount.load(std::memory_order_relaxed);
  _instance->_connected.store(newCount > 0, std::memory_order_relaxed);
  DBGF("[MIDI] Disconnected (ssrc=%u), sessions=%d\n", ssrc, newCount);
}

void MidiEngine::_onNoteOn(byte channel, byte note, byte velocity) {
  if (!_instance) return;
  _instance->_notesReceived.fetch_add(1, std::memory_order_relaxed);
  _instance->_lastActivity.store(millis(), std::memory_order_relaxed);
  if (!_instance->isChannelAllowed(channel)) return;

  // MIDI spec: NoteOn with velocity=0 should be treated as NoteOff
  if (velocity == 0) {
    _onNoteOff(channel, note, 64);
    return;
  }

  if (_instance->_usePipelineMode()) {
    MidiEvent ev;
    ev.type = MIDI_EVT_NOTE_ON;
    ev.channel = channel;
    ev.data1 = note;
    ev.data2 = velocity;
    ev.timestamp = micros();
    _instance->_eventProc->processMidiEvent(ev);
  } else {
    _instance->_instrumentMgr->onNoteOn(channel, note, velocity);
  }
}

void MidiEngine::_onNoteOff(byte channel, byte note, byte velocity) {
  if (!_instance) return;
  _instance->_notesReceived.fetch_add(1, std::memory_order_relaxed);
  _instance->_lastActivity.store(millis(), std::memory_order_relaxed);
  if (!_instance->isChannelAllowed(channel)) return;

  if (_instance->_usePipelineMode()) {
    MidiEvent ev;
    ev.type = MIDI_EVT_NOTE_OFF;
    ev.channel = channel;
    ev.data1 = note;
    ev.data2 = velocity;  // Pass through NoteOff velocity for noteOffActions
    ev.timestamp = micros();
    _instance->_eventProc->processMidiEvent(ev);
  } else {
    _instance->_instrumentMgr->onNoteOff(channel, note);
  }
}

void MidiEngine::_onControlChange(byte channel, byte number, byte value) {
  if (!_instance) return;
  _instance->_lastActivity.store(millis(), std::memory_order_relaxed);
  if (!_instance->isChannelAllowed(channel)) return;

  if (_instance->_usePipelineMode()) {
    MidiEvent ev;
    ev.type = MIDI_EVT_CC;
    ev.channel = channel;
    ev.data1 = number;
    ev.data2 = value;
    ev.timestamp = micros();
    _instance->_eventProc->processMidiEvent(ev);
  } else {
    _instance->_instrumentMgr->onControlChange(channel, number, value);
  }
}

void MidiEngine::_onPitchBend(byte channel, int value) {
  if (!_instance) return;
  _instance->_lastActivity.store(millis(), std::memory_order_relaxed);
  if (!_instance->isChannelAllowed(channel)) return;

  if (_instance->_usePipelineMode()) {
    MidiEvent ev;
    ev.type = MIDI_EVT_PITCH_BEND;
    ev.channel = channel;
    ev.data1 = (uint8_t)((value >> 7) & 0x7F); // MSB
    ev.data2 = (uint8_t)(value & 0x7F);         // LSB
    ev.timestamp = micros();
    _instance->_eventProc->processMidiEvent(ev);
  } else {
    _instance->_instrumentMgr->onPitchBend(channel, (int16_t)value);
  }
}

void MidiEngine::_onAftertouch(byte channel, byte pressure) {
  if (!_instance) return;
  _instance->_lastActivity.store(millis(), std::memory_order_relaxed);
  if (!_instance->isChannelAllowed(channel)) return;

  if (_instance->_usePipelineMode()) {
    MidiEvent ev;
    ev.type = MIDI_EVT_AFTERTOUCH;
    ev.channel = channel;
    ev.data1 = pressure;
    ev.data2 = 0;
    ev.timestamp = micros();
    _instance->_eventProc->processMidiEvent(ev);
  } else {
    _instance->_instrumentMgr->onAftertouch(channel, pressure);
  }
}
