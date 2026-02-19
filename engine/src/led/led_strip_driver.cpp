#include "led_strip_driver.h"
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>

// ============================================================================
// WS2812B timing via ESP32 RMT (hardware, zero CPU)
// ============================================================================
// WS2812B protocol: 800kHz, 1.25us per bit
//   T0H = 0.4us (320ns), T0L = 0.85us (850ns)
//   T1H = 0.8us (800ns), T1L = 0.45us (450ns)
//   Reset: >50us low
//
// Uses ESP-IDF 5.x RMT driver directly (rmt_new_tx_channel +
// rmt_new_bytes_encoder) — no dependency on espShow/neopixelWrite.
// ============================================================================

// RMT resolution: 10MHz = 100ns per tick
static const uint32_t RMT_RESOLUTION_HZ = 10000000;

// WS2812B bit timing in RMT ticks (100ns each)
// Bit 0: 3 ticks high (300ns), 9 ticks low (900ns)
// Bit 1: 9 ticks high (900ns), 3 ticks low (300ns)
static const rmt_bytes_encoder_config_t _ws2812bEncoderConfig = {
  .bit0 = {
    .duration0 = 3,
    .level0 = 1,
    .duration1 = 9,
    .level1 = 0,
  },
  .bit1 = {
    .duration0 = 9,
    .level0 = 1,
    .duration1 = 3,
    .level1 = 0,
  },
  .flags = {
    .msb_first = 1,
  },
};

LedStripDriver::LedStripDriver() {
  for (uint8_t i = 0; i < LED_MAX_STRIPS; i++) {
    _strips[i].buffer = nullptr;
    _strips[i].ledCount = 0;
    _strips[i].gpioPin = 0xFF;
    _strips[i].brightness = 128;
    _strips[i].active = false;
    _strips[i].rmtChannel = nullptr;
    _strips[i].rmtEncoder = nullptr;
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

  // Configure RMT TX channel
  rmt_tx_channel_config_t txConfig = {};
  txConfig.gpio_num = (gpio_num_t)config.gpioPin;
  txConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  txConfig.resolution_hz = RMT_RESOLUTION_HZ;
  txConfig.mem_block_symbols = 64;  // 64 symbols per block (minimum)
  txConfig.trans_queue_depth = 1;
  txConfig.flags.invert_out = false;
  txConfig.flags.with_dma = false;

  esp_err_t err = rmt_new_tx_channel(&txConfig, &_strips[stripIdx].rmtChannel);
  if (err != ESP_OK) {
    DBGF("[LED] Strip %d: RMT channel creation failed (err=%d)\n", stripIdx, err);
    delete[] _strips[stripIdx].buffer;
    _strips[stripIdx].buffer = nullptr;
    return false;
  }

  // Create bytes encoder for WS2812B timing
  err = rmt_new_bytes_encoder(&_ws2812bEncoderConfig, &_strips[stripIdx].rmtEncoder);
  if (err != ESP_OK) {
    DBGF("[LED] Strip %d: RMT encoder creation failed (err=%d)\n", stripIdx, err);
    rmt_del_channel(_strips[stripIdx].rmtChannel);
    _strips[stripIdx].rmtChannel = nullptr;
    delete[] _strips[stripIdx].buffer;
    _strips[stripIdx].buffer = nullptr;
    return false;
  }

  // Enable the RMT channel
  rmt_enable(_strips[stripIdx].rmtChannel);

  _strips[stripIdx].active = true;

  DBGF("[LED] Strip %d initialized: %d pixels on GPIO %d (brightness max=%d)\n",
       stripIdx, config.ledCount, config.gpioPin, config.maxBrightness);
  return true;
}

void LedStripDriver::endStrip(uint8_t stripIdx) {
  if (stripIdx >= LED_MAX_STRIPS) return;

  if (_strips[stripIdx].active) {
    // Clear all pixels before releasing
    clearStrip(stripIdx);
    show(stripIdx);
  }

  // Release RMT resources
  if (_strips[stripIdx].rmtChannel) {
    rmt_disable(_strips[stripIdx].rmtChannel);
    rmt_del_channel(_strips[stripIdx].rmtChannel);
    _strips[stripIdx].rmtChannel = nullptr;
  }
  if (_strips[stripIdx].rmtEncoder) {
    rmt_del_encoder(_strips[stripIdx].rmtEncoder);
    _strips[stripIdx].rmtEncoder = nullptr;
  }

  if (_strips[stripIdx].buffer) {
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
// RMT transmission via ESP-IDF 5.x rmt_transmit()
// ============================================================================

void LedStripDriver::_rmtSend(uint8_t stripIdx) {
  StripState& strip = _strips[stripIdx];
  if (!strip.buffer || strip.ledCount == 0) return;
  if (!strip.rmtChannel || !strip.rmtEncoder) return;

  // Convert RGB buffer to GRB for WS2812B wire format
  uint16_t numBytes = strip.ledCount * 3;
  uint8_t* grb = (uint8_t*)alloca(numBytes);

  for (uint16_t i = 0; i < strip.ledCount; i++) {
    uint16_t off = i * 3;
    grb[off + 0] = strip.buffer[off + 1]; // G
    grb[off + 1] = strip.buffer[off + 0]; // R
    grb[off + 2] = strip.buffer[off + 2]; // B
  }

  // Transmit via RMT with >50us reset signal at end
  rmt_transmit_config_t txCfg = {};
  txCfg.loop_count = 0;  // No looping
  txCfg.flags.eot_level = 0;  // End-of-transmission level = LOW (reset)

  rmt_transmit(strip.rmtChannel, strip.rmtEncoder, grb, numBytes, &txCfg);
  rmt_tx_wait_all_done(strip.rmtChannel, 100);  // Wait max 100ms
}
