#include "mic_inmp441.h"

MicINMP441::MicINMP441()
  : _connected(false), _level(0), _peakAccum(0),
    _triggered(false), _triggerTimestampUs(0), _triggerPeak(0),
    _triggerThreshold(MIC_DEFAULT_THRESHOLD), _triggerArmed(false) {}

bool MicINMP441::begin() {
  i2s_config_t i2sConfig = {};
  i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2sConfig.sample_rate = MIC_SAMPLE_RATE;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sConfig.dma_buf_count = MIC_DMA_BUF_COUNT;
  i2sConfig.dma_buf_len = MIC_DMA_BUF_LEN;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = false;
  i2sConfig.fixed_mclk = 0;

  esp_err_t err = i2s_driver_install(MIC_I2S_NUM, &i2sConfig, 0, NULL);
  if (err != ESP_OK) {
    DBGF("[Mic] I2S driver install failed: %d\n", err);
    return false;
  }

  i2s_pin_config_t pinConfig = {};
  pinConfig.bck_io_num = MIC_I2S_SCK;
  pinConfig.ws_io_num = MIC_I2S_WS;
  pinConfig.data_in_num = MIC_I2S_SD;
  pinConfig.data_out_num = I2S_PIN_NO_CHANGE;

  err = i2s_set_pin(MIC_I2S_NUM, &pinConfig);
  if (err != ESP_OK) {
    DBGF("[Mic] I2S pin config failed: %d\n", err);
    i2s_driver_uninstall(MIC_I2S_NUM);
    return false;
  }

  i2s_zero_dma_buffer(MIC_I2S_NUM);

  // Try reading a buffer to detect if mic is connected
  // INMP441 outputs non-zero data when powered; all-zero = not connected
  delay(50);  // Let I2S stabilize
  int32_t testBuf[64];
  size_t bytesRead = 0;
  i2s_read(MIC_I2S_NUM, testBuf, sizeof(testBuf), &bytesRead, 100);

  bool hasSignal = false;
  if (bytesRead > 0) {
    for (size_t i = 0; i < bytesRead / sizeof(int32_t); i++) {
      // INMP441 outputs 24-bit data left-aligned in 32-bit word
      int32_t sample = testBuf[i] >> 8;
      if (sample != 0) { hasSignal = true; break; }
    }
  }

  if (!hasSignal) {
    DBGLN("[Mic] INMP441 not detected (no signal on I2S)");
    i2s_driver_uninstall(MIC_I2S_NUM);
    _connected = false;
    return false;
  }

  _connected = true;
  DBGLN("[Mic] INMP441 detected and ready");
  return true;
}

void MicINMP441::end() {
  if (_connected) {
    i2s_driver_uninstall(MIC_I2S_NUM);
    _connected = false;
  }
}

uint32_t MicINMP441::readPeak() {
  if (!_connected) return 0;

  int32_t buf[MIC_DMA_BUF_LEN];
  size_t bytesRead = 0;
  esp_err_t err = i2s_read(MIC_I2S_NUM, buf, sizeof(buf), &bytesRead, 10);
  if (err != ESP_OK || bytesRead == 0) return 0;

  uint32_t peak = 0;
  size_t samples = bytesRead / sizeof(int32_t);

  for (size_t i = 0; i < samples; i++) {
    // INMP441: 24-bit data, left-aligned in 32-bit word
    int32_t sample = buf[i] >> 8;
    uint32_t absSample = (sample < 0) ? (uint32_t)(-sample) : (uint32_t)sample;
    if (absSample > peak) peak = absSample;
  }

  // Update level (exponential moving average, scaled 0-100)
  // 24-bit max = ~8388607, but typical peaks are much lower
  uint8_t newLevel = (peak > 100000) ? 100 : (uint8_t)(peak / 1000);
  _level = (_level * 3 + newLevel) / 4;

  // Check trigger
  if (_triggerArmed && !_triggered && peak >= _triggerThreshold) {
    _triggered = true;
    _triggerTimestampUs = micros();
    _triggerPeak = peak;
  }

  return peak;
}

void MicINMP441::armTrigger(uint16_t threshold) {
  _triggerThreshold = threshold;
  _triggered = false;
  _triggerTimestampUs = 0;
  _triggerPeak = 0;
  _triggerArmed = true;
}

void MicINMP441::disarmTrigger() {
  _triggerArmed = false;
  _triggered = false;
}
