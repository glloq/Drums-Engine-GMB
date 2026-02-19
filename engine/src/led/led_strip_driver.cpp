#include "led_strip_driver.h"
#include <driver/rmt_tx.h>

// espShow() from Arduino ESP32 Core 3.x (esp32-hal-rgb-led.c)
// Sends raw WS2812B bitstream via RMT hardware.
// type: 0 = WS2812B
extern "C" void espShow(uint8_t pin, uint8_t* pixels, uint32_t numBytes, uint8_t type);

// ============================================================================
// WS2812B timing via ESP32 RMT (hardware, zero CPU)
// ============================================================================
// WS2812B protocol: 800kHz, 1.25us per bit
//   T0H = 0.4us (320ns), T0L = 0.85us (850ns)
//   T1H = 0.8us (800ns), T1L = 0.45us (450ns)
//   Reset: >50us low
//
// Using neopixelWrite() from ESP32 Arduino Core 3.x which handles
// RMT channel allocation and timing internally.
// ============================================================================

LedStripDriver::LedStripDriver() {
  for (uint8_t i = 0; i < LED_MAX_STRIPS; i++) {
    _strips[i].buffer = nullptr;
    _strips[i].ledCount = 0;
    _strips[i].gpioPin = 0xFF;
    _strips[i].brightness = 128;
    _strips[i].active = false;
  }
}

bool LedStripDriver::beginStrip(uint8_t stripIdx, const LedStripConfig& config) {
  if (stripIdx >= LED_MAX_STRIPS) return false;
  if (config.ledCount == 0 || config.ledCount > LED_MAX_PIXELS) return false;

  // Liberer si deja actif
  endStrip(stripIdx);

  // Allouer le buffer (3 octets par pixel: R, G, B)
  _strips[stripIdx].buffer = new uint8_t[config.ledCount * 3];
  if (!_strips[stripIdx].buffer) {
    DBGF("[LED] Failed to allocate buffer for strip %d (%d pixels)\n",
         stripIdx, config.ledCount);
    return false;
  }
  memset(_strips[stripIdx].buffer, 0, config.ledCount * 3);

  _strips[stripIdx].ledCount = config.ledCount;
  _strips[stripIdx].gpioPin = config.gpioPin;
  _strips[stripIdx].brightness = config.maxBrightness;
  _strips[stripIdx].active = true;

  // Configure GPIO pin for output
  pinMode(config.gpioPin, OUTPUT);
  digitalWrite(config.gpioPin, LOW);

  DBGF("[LED] Strip %d initialized: %d pixels on GPIO %d (brightness max=%d)\n",
       stripIdx, config.ledCount, config.gpioPin, config.maxBrightness);
  return true;
}

void LedStripDriver::endStrip(uint8_t stripIdx) {
  if (stripIdx >= LED_MAX_STRIPS) return;
  if (_strips[stripIdx].buffer) {
    // Clear all pixels before releasing
    clearStrip(stripIdx);
    show(stripIdx);
    delete[] _strips[stripIdx].buffer;
    _strips[stripIdx].buffer = nullptr;
  }
  _strips[stripIdx].active = false;
  _strips[stripIdx].ledCount = 0;
}

void LedStripDriver::setPixel(uint8_t stripIdx, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b) {
  if (stripIdx >= LED_MAX_STRIPS || !_strips[stripIdx].active) return;
  if (pixel >= _strips[stripIdx].ledCount) return;

  // Apply brightness scaling
  uint8_t bri = _strips[stripIdx].brightness;
  if (bri < 255) {
    r = (uint16_t)r * bri / 255;
    g = (uint16_t)g * bri / 255;
    b = (uint16_t)b * bri / 255;
  }

  uint16_t offset = pixel * 3;
  _strips[stripIdx].buffer[offset + 0] = r;
  _strips[stripIdx].buffer[offset + 1] = g;
  _strips[stripIdx].buffer[offset + 2] = b;
}

void LedStripDriver::setPixel(uint8_t stripIdx, uint16_t pixel, const RgbColor& color) {
  setPixel(stripIdx, pixel, color.r, color.g, color.b);
}

void LedStripDriver::clearStrip(uint8_t stripIdx) {
  if (stripIdx >= LED_MAX_STRIPS || !_strips[stripIdx].active) return;
  memset(_strips[stripIdx].buffer, 0, _strips[stripIdx].ledCount * 3);
}

void LedStripDriver::clearAll() {
  for (uint8_t i = 0; i < LED_MAX_STRIPS; i++) {
    clearStrip(i);
  }
}

void LedStripDriver::show(uint8_t stripIdx) {
  if (stripIdx >= LED_MAX_STRIPS || !_strips[stripIdx].active) return;
  _rmtSend(stripIdx);
}

void LedStripDriver::showAll() {
  for (uint8_t i = 0; i < LED_MAX_STRIPS; i++) {
    if (_strips[i].active) {
      _rmtSend(i);
    }
  }
}

void LedStripDriver::setStripBrightness(uint8_t stripIdx, uint8_t brightness) {
  if (stripIdx >= LED_MAX_STRIPS) return;
  _strips[stripIdx].brightness = brightness;
}

uint16_t LedStripDriver::getPixelCount(uint8_t stripIdx) const {
  if (stripIdx >= LED_MAX_STRIPS) return 0;
  return _strips[stripIdx].ledCount;
}

bool LedStripDriver::isActive(uint8_t stripIdx) const {
  if (stripIdx >= LED_MAX_STRIPS) return false;
  return _strips[stripIdx].active;
}

// ============================================================================
// RMT transmission — utilise neopixelWrite() de ESP32 Arduino Core 3.x
// ============================================================================
// neopixelWrite(pin, r, g, b) n'envoie qu'un pixel.
// Pour un strip entier, on utilise rmt_transmit() directement.
// Alternativement, on envoie pixel par pixel via le protocole WS2812B.
//
// Ici on utilise la methode bas niveau avec le buffer brut et
// l'API ESP-IDF RMT pour envoyer toute la trame d'un coup.
// ============================================================================

void LedStripDriver::_rmtSend(uint8_t stripIdx) {
  StripState& strip = _strips[stripIdx];
  if (!strip.buffer || strip.ledCount == 0) return;

  // Use neopixelWrite for each pixel (ESP32 Arduino Core 3.x)
  // The ESP32 Arduino Core handles RMT internally.
  // For performance with many LEDs, we write the raw WS2812B bitstream
  // using the espShow() internal function exposed by the core.

  // ESP32 Arduino Core 3.x provides rmt_data_t and rmtWrite()
  // But the simplest reliable approach is via the Adafruit NeoPixel
  // library's ESP32 RMT backend, or direct espShow().

  // Use the low-level espShow() from the Arduino ESP32 core
  // It takes: pin, pixel_data (GRB order), data_length_bytes
  // Convert our RGB buffer to GRB for WS2812B
  uint16_t numBytes = strip.ledCount * 3;
  uint8_t* grb = (uint8_t*)alloca(numBytes);

  for (uint16_t i = 0; i < strip.ledCount; i++) {
    uint16_t off = i * 3;
    grb[off + 0] = strip.buffer[off + 1]; // G
    grb[off + 1] = strip.buffer[off + 0]; // R
    grb[off + 2] = strip.buffer[off + 2]; // B
  }

  espShow(strip.gpioPin, grb, numBytes, 0);
}
