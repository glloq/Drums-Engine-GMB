#include "midi_engine.h"

// AppleMIDI global instance (requis par la librairie)
USING_NAMESPACE_APPLEMIDI
APPLEMIDI_CREATE_DEFAULTSESSION_INSTANCE();

MidiEngine* MidiEngine::_instance = nullptr;

MidiEngine::MidiEngine(EventProcessor* eventProc, InstrumentManager* instrumentMgr)
  : _eventProc(eventProc), _instrumentMgr(instrumentMgr),
    _connected(false), _sessionCount(0),
    _lastActivity(0), _notesReceived(0), _notesSent(0) {
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
  MIDI.setHandleSystemExclusive(_onSysEx);

  DBGF("[MIDI] Session '%s' on port %d\n", MIDI_SESSION_NAME, RTP_MIDI_PORT);
  return true;
}

void MidiEngine::update() {
  MIDI.read();

  // Pousser la notification GMB (bloc 0x11) depuis CE contexte, et pas depuis
  // Core 0 : la pile AppleMIDI n'est pas reentrante, et elle est deja lue ici.
  // Le cas courant est un test de drapeau et rien d'autre.
  if (_gmb && _gmb->notificationPending()) {
    uint8_t frame[GMB_NOTIFICATION_SIZE];
    const size_t n = _gmb->takeNotification(frame, sizeof(frame), millis());
    if (n > 0) sendSysEx(frame, n);
  }
}

void MidiEngine::sendSysEx(const uint8_t* data, size_t len) {
  if (!data || len < 2) return;
  // `true` : le tableau porte deja ses delimiteurs F0/F7, la librairie l'emet
  // tel quel. C'est indispensable ici — les trames GMB sont construites
  // completes par gmb_sysex.cpp et doivent partir octet pour octet.
  MIDI.sendSysEx((unsigned)len, data, true);
  _sysexSent.fetch_add(1, std::memory_order_relaxed);
  _lastActivity = millis();
}

void MidiEngine::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  MIDI.sendNoteOn(note, velocity, channel);
  _notesSent.fetch_add(1, std::memory_order_relaxed);
  _lastActivity = millis();

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
  _lastActivity = millis();

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
}

bool MidiEngine::_usePipelineMode() const {
  return _eventProc && _eventProc->getLookup().pipeline_count > 0;
}

// --- Callbacks statiques ---

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
  _instance->_notesReceived.fetch_add(1, std::memory_order_relaxed);
  _instance->_lastActivity = millis();
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
  _instance->_lastActivity = millis();
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
  _instance->_lastActivity = millis();
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
  _instance->_lastActivity = millis();
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

// Reception SysEx. Appele depuis MIDI.read(), donc sur le coeur temps reel :
// le traitement doit rester borne et sans effet de bord. Il l'est — le service
// n'analyse que l'en-tete et recopie un segment deja en cache. Aucune frappe
// n'est planifiee, aucune note active n'est touchee, le scheduler n'est pas
// sollicite : la decouverte GMB ne peut pas faire bouger la machine.
void MidiEngine::_onSysEx(byte* data, unsigned size) {
  if (!_instance) return;
  _instance->_lastActivity = millis();
  if (!_instance->_gmb) return;

  uint8_t response[GmbSysExService::MAX_RESPONSE];
  const size_t n = _instance->_gmb->handleSysEx((const uint8_t*)data, (size_t)size,
                                                response, sizeof(response), millis());
  if (n > 0) _instance->sendSysEx(response, n);
}

void MidiEngine::_onAftertouch(byte channel, byte pressure) {
  if (!_instance) return;
  _instance->_lastActivity = millis();
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
