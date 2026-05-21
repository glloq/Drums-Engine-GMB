#include "uart_midi.h"

UartMidi::UartMidi() {}

bool UartMidi::begin(MidiEngine* engine, uint32_t baud, int8_t rxPin, int8_t txPin) {
  _engine = engine;
  // Serial2 maps to UART2 on ESP32. invert=false, downlink only needed.
  Serial2.begin(baud, SERIAL_8N1, rxPin, txPin);
  Serial2.setRxBufferSize(UART_MIDI_RX_BUFFER);
  _enabled = true;
  DBGF("[MIDI-UART] Serial2 @ %u baud on RX=%d TX=%d (buf %d)\n",
       baud, rxPin, txPin, UART_MIDI_RX_BUFFER);
  return true;
}

void UartMidi::update() {
  if (!_enabled || !_engine) return;
  // Read available bytes in a tight loop (zero allocation).
  while (Serial2.available() > 0) {
    int b = Serial2.read();
    if (b < 0) break;
    _bytesRx++;
    _processByte((uint8_t)b);
  }
}

void UartMidi::_processByte(uint8_t b) {
  // Real-time messages (0xF8..0xFF) are single-byte and may appear anywhere.
  // We ignore clock/sensing here (no clock sync feature). Active Sensing 0xFE
  // is silently dropped to avoid spamming.
  if (b >= 0xF8) {
    return;
  }

  // Status byte: bit7 set
  if (b & 0x80) {
    if (b == 0xF0) {
      _inSysEx = true;       // SysEx start — consume until 0xF7
      _haveData1 = false;
      return;
    }
    if (b == 0xF7) {
      _inSysEx = false;      // SysEx end
      return;
    }
    // System Common (0xF1..0xF6): no Running Status carry over.
    if (b >= 0xF1 && b <= 0xF6) {
      _status = 0;
      _haveData1 = false;
      return;
    }
    // Channel Voice status — store and reset data accumulator
    _status = b;
    _haveData1 = false;
    return;
  }

  // Data byte (bit7 clear)
  if (_inSysEx) {
    return;  // ignore SysEx payload
  }
  if (_status == 0) {
    return;  // no status yet (Running Status uninitialised) → byte discarded
  }

  uint8_t kind = _status & 0xF0;
  // 1-data-byte messages: Program Change (0xC0), Channel Aftertouch (0xD0)
  if (kind == 0xC0 || kind == 0xD0) {
    _dispatch(_status, b, 0);
    return;
  }

  // 2-data-byte messages: Note Off, Note On, Poly Aftertouch, CC, Pitch Bend
  if (!_haveData1) {
    _data1 = b;
    _haveData1 = true;
    return;
  }
  _dispatch(_status, _data1, b);
  _haveData1 = false;  // ready for next message (Running Status keeps _status)
}

void UartMidi::_dispatch(uint8_t status, uint8_t d1, uint8_t d2) {
  if (!_engine) return;

  uint8_t kind = status & 0xF0;
  uint8_t channel = (status & 0x0F) + 1;  // 1..16 (project convention)

  MidiEvent ev;
  ev.channel = channel;
  ev.data1 = d1;
  ev.data2 = d2;
  ev.timestamp = micros();

  switch (kind) {
    case 0x80: ev.type = MIDI_EVT_NOTE_OFF; break;
    case 0x90: ev.type = MIDI_EVT_NOTE_ON;  break;
    case 0xA0: ev.type = MIDI_EVT_POLY_AFTERTOUCH; break;
    case 0xB0: ev.type = MIDI_EVT_CC; break;
    case 0xD0: ev.type = MIDI_EVT_AFTERTOUCH; ev.data1 = d1; ev.data2 = 0; break;
    case 0xE0: ev.type = MIDI_EVT_PITCH_BEND; break;
    default:   return;  // Program Change / unsupported: ignored for percussion
  }

  _msgsRx++;
  _engine->ingestExternalMidi(ev);
}
