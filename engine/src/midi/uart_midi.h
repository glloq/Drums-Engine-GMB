#ifndef UART_MIDI_H
#define UART_MIDI_H

#include <Arduino.h>
#include <atomic>
#include "../core/config.h"
#include "../core/types.h"
#include "midi_engine.h"

// ============================================================================
// UartMidi - Hardware UART MIDI receiver (DIN/TRS, 31250 baud)
// ============================================================================
// Parses the MIDI byte stream from Serial2 (configurable pins). Supports
// Running Status, system messages, and ignores active sensing. Dispatches
// fully-decoded events to MidiEngine::ingestExternalMidi.
//
// Resilience: a fixed-size SoftwareSerial / HardwareSerial RX buffer absorbs
// short bursts (drumrolls). On buffer overflow, the byte parser resyncs on the
// next status byte and increments midi_engine overflow counter.
// ============================================================================

class UartMidi {
public:
  UartMidi();

  // Initialise Serial2 at MIDI baud rate. Returns false if unavailable.
  bool begin(MidiEngine* engine,
             uint32_t baud = UART_MIDI_BAUD,
             int8_t rxPin = UART_MIDI_RX_PIN,
             int8_t txPin = UART_MIDI_TX_PIN);

  // Drain UART RX, parse, and dispatch events. Call frequently from RT task.
  void update();

  bool isEnabled() const { return _enabled; }
  uint32_t getBytesReceived() const { return _bytesRx; }
  uint32_t getMessagesDispatched() const { return _msgsRx; }

private:
  MidiEngine* _engine = nullptr;
  bool _enabled = false;

  // Running Status / parse state
  uint8_t _status = 0;          // last channel-voice status byte
  uint8_t _data1 = 0;
  bool _haveData1 = false;
  bool _inSysEx = false;        // currently inside a SysEx block (ignored)

  uint32_t _bytesRx = 0;
  uint32_t _msgsRx = 0;

  void _processByte(uint8_t b);
  void _dispatch(uint8_t status, uint8_t d1, uint8_t d2);
};

#endif // UART_MIDI_H
