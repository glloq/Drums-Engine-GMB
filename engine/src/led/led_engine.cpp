#include "led_engine.h"

// ============================================================================
// LedEngine - Implementation
// ============================================================================

LedEngine::LedEngine()
  : _segmentCount(0), _themeCount(0), _activeThemeId(0xFF),
    _masterBrightness(128), _lastFrameMs(0), _frameCount(0) {
  memset(_hitStates, 0, sizeof(_hitStates));
  memset(_noteLookup, 0, sizeof(_noteLookup));
  for (uint8_t i = 0; i < 128; i++) {
    _noteLookup[i].count = 0;
  }
}

bool LedEngine::begin() {
  _lastFrameMs = millis();
  DBGLN("[LED] LedEngine initialized");
  return true;
}

void LedEngine::update() {
  uint32_t now = millis();

  // Limiter a LED_FPS
  uint32_t frameInterval = 1000 / LED_FPS;
  if (now - _lastFrameMs < frameInterval) return;
  _lastFrameMs = now;
  _frameCount++;

  // 1. Consommer les events MIDI de la queue
  _processEvents();

  // 2. Rendre chaque segment
  _renderFrame();

  // 3. Envoyer aux strips
  _driver.showAll();
}

// ============================================================================
// Configuration strips
// ============================================================================

bool LedEngine::configureStrip(uint8_t stripIdx, const LedStripConfig& config) {
  if (stripIdx >= LED_MAX_STRIPS) return false;

  _stripConfigs[stripIdx] = config;
  _stripConfigs[stripIdx].id = stripIdx;

  if (config.enabled && config.ledCount > 0) {
    return _driver.beginStrip(stripIdx, config);
  } else {
    _driver.endStrip(stripIdx);
    return true;
  }
}

bool LedEngine::removeStrip(uint8_t stripIdx) {
  if (stripIdx >= LED_MAX_STRIPS) return false;
  _driver.endStrip(stripIdx);
  _stripConfigs[stripIdx] = LedStripConfig();
  return true;
}

uint8_t LedEngine::getActiveStripCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < LED_MAX_STRIPS; i++) {
    if (_stripConfigs[i].enabled && _driver.isActive(i)) count++;
  }
  return count;
}

// ============================================================================
// Configuration segments
// ============================================================================

int LedEngine::addSegment(const LedSegmentConfig& config) {
  if (_segmentCount >= LED_MAX_SEGMENTS) return -1;

  LedSegmentConfig seg = config;
  if (seg.id == 0) seg.id = _nextSegmentId();

  _segments[_segmentCount] = seg;
  _segmentCount++;

  rebuildNoteLookup();
  return _segmentCount - 1;
}

bool LedEngine::updateSegment(uint8_t id, const LedSegmentConfig& config) {
  int8_t idx = _findSegmentIndex(id);
  if (idx < 0) return false;

  _segments[idx] = config;
  _segments[idx].id = id;

  rebuildNoteLookup();
  return true;
}

bool LedEngine::removeSegment(uint8_t id) {
  int8_t idx = _findSegmentIndex(id);
  if (idx < 0) return false;

  for (uint8_t i = idx; i < _segmentCount - 1; i++) {
    _segments[i] = _segments[i + 1];
    _hitStates[i] = _hitStates[i + 1];
  }
  _segmentCount--;
  _hitStates[_segmentCount] = {};

  rebuildNoteLookup();
  return true;
}

LedSegmentConfig* LedEngine::getSegment(uint8_t id) {
  int8_t idx = _findSegmentIndex(id);
  return (idx >= 0) ? &_segments[idx] : nullptr;
}

LedSegmentConfig* LedEngine::getSegmentByIndex(uint8_t index) {
  return (index < _segmentCount) ? &_segments[index] : nullptr;
}

// ============================================================================
// Themes
// ============================================================================

int LedEngine::addTheme(const LedThemeConfig& config) {
  if (_themeCount >= LED_MAX_THEMES) return -1;

  LedThemeConfig theme = config;
  if (theme.id == 0) theme.id = _nextThemeId();

  _themes[_themeCount] = theme;
  _themeCount++;

  // Auto-activer si c'est le premier theme
  if (_themeCount == 1) _activeThemeId = theme.id;

  return _themeCount - 1;
}

bool LedEngine::updateTheme(uint8_t id, const LedThemeConfig& config) {
  int8_t idx = _findThemeIndex(id);
  if (idx < 0) return false;

  _themes[idx] = config;
  _themes[idx].id = id;
  return true;
}

bool LedEngine::removeTheme(uint8_t id) {
  int8_t idx = _findThemeIndex(id);
  if (idx < 0) return false;

  for (uint8_t i = idx; i < _themeCount - 1; i++) {
    _themes[i] = _themes[i + 1];
  }
  _themeCount--;

  if (_activeThemeId == id) {
    _activeThemeId = (_themeCount > 0) ? _themes[0].id : 0xFF;
  }
  return true;
}

LedThemeConfig* LedEngine::getTheme(uint8_t id) {
  int8_t idx = _findThemeIndex(id);
  return (idx >= 0) ? &_themes[idx] : nullptr;
}

LedThemeConfig* LedEngine::getThemeByIndex(uint8_t index) {
  return (index < _themeCount) ? &_themes[index] : nullptr;
}

void LedEngine::setActiveTheme(uint8_t themeId) {
  _activeThemeId = themeId;
}

void LedEngine::setMasterBrightness(uint8_t brightness) {
  _masterBrightness = brightness;
  for (uint8_t i = 0; i < LED_MAX_STRIPS; i++) {
    if (_driver.isActive(i)) {
      _driver.setStripBrightness(i, brightness);
    }
  }
}

// ============================================================================
// Test
// ============================================================================

void LedEngine::testSegment(uint8_t segmentId, uint8_t brightness) {
  int8_t idx = _findSegmentIndex(segmentId);
  if (idx < 0) return;

  const LedSegmentConfig& seg = _segments[idx];
  for (uint16_t p = 0; p < seg.pixelCount; p++) {
    _driver.setPixel(seg.stripId, seg.pixelStart + p,
                     brightness, brightness, brightness);
  }
  _driver.show(seg.stripId);

  DBGF("[LED] Test segment %d: %d pixels white\n", segmentId, seg.pixelCount);
}

void LedEngine::testStrip(uint8_t stripIdx) {
  if (stripIdx >= LED_MAX_STRIPS || !_driver.isActive(stripIdx)) return;

  uint16_t count = _driver.getPixelCount(stripIdx);
  for (uint16_t p = 0; p < count; p++) {
    _driver.setPixel(stripIdx, p, 255, 255, 255);
  }
  _driver.show(stripIdx);

  DBGF("[LED] Test strip %d: %d pixels white\n", stripIdx, count);
}

// ============================================================================
// Lookup note → segment
// ============================================================================

void LedEngine::rebuildNoteLookup() {
  // Reset
  for (uint8_t i = 0; i < 128; i++) {
    _noteLookup[i].count = 0;
  }
  // Le remplissage se fait via registerNoteSegment() appele depuis main.cpp
}

void LedEngine::registerNoteSegment(uint8_t midiNote, uint8_t segmentIndex) {
  if (midiNote >= 128 || segmentIndex >= _segmentCount) return;
  NoteLookup& nl = _noteLookup[midiNote];
  if (nl.count >= MAX_SEGMENTS_PER_NOTE) return;
  nl.segmentIndices[nl.count] = segmentIndex;
  nl.count++;
}

// ============================================================================
// Processing events MIDI
// ============================================================================

void LedEngine::_processEvents() {
  LedEvent event;
  while (_eventQueue.pop(event)) {
    if (event.noteOn) {
      // Trouver les segments associes a cette note
      NoteLookup& nl = _noteLookup[event.midiNote];
      for (uint8_t i = 0; i < nl.count; i++) {
        uint8_t segIdx = nl.segmentIndices[i];
        if (segIdx >= _segmentCount) continue;
        if (!_segments[segIdx].enabled) continue;

        _hitStates[segIdx].active = true;
        _hitStates[segIdx].velocity = event.velocity;
        _hitStates[segIdx].startMs = millis();
        _hitStates[segIdx].durationMs = _segments[segIdx].hitFadeDurationMs;
      }
    }
    // NoteOff: on laisse le fade se terminer naturellement
  }
}

// ============================================================================
// Rendu
// ============================================================================

void LedEngine::_renderFrame() {
  for (uint8_t i = 0; i < _segmentCount; i++) {
    _renderSegment(i);
  }
}

void LedEngine::_renderSegment(uint8_t segIdx) {
  const LedSegmentConfig& seg = _segments[segIdx];
  if (!seg.enabled) return;
  if (seg.stripId >= LED_MAX_STRIPS || !_driver.isActive(seg.stripId)) return;

  uint32_t now = millis();

  // Calculer la couleur idle
  RgbColor idleColor = _computeIdleColor(segIdx, now);

  // Calculer la couleur hit (si actif)
  RgbColor hitColor(0, 0, 0);
  uint8_t hitAlpha = 0;

  if (_hitStates[segIdx].active) {
    uint32_t elapsed = now - _hitStates[segIdx].startMs;

    if (elapsed >= _hitStates[segIdx].durationMs) {
      // Fade termine
      _hitStates[segIdx].active = false;
    } else {
      hitColor = _computeHitColor(segIdx, elapsed);

      // Alpha = intensite du hit (decroit lineairement)
      uint16_t remaining = _hitStates[segIdx].durationMs - elapsed;
      hitAlpha = (uint32_t)remaining * 255 / _hitStates[segIdx].durationMs;

      // Moduler par velocity si configure
      if (seg.velocityToBrightness) {
        hitAlpha = (uint16_t)hitAlpha * _hitStates[segIdx].velocity / 127;
      }
    }
  }

  // Blend hit + idle
  RgbColor finalColor = _blendColors(idleColor, hitColor, hitAlpha);

  // Appliquer le master brightness
  if (_masterBrightness < 255) {
    finalColor.r = (uint16_t)finalColor.r * _masterBrightness / 255;
    finalColor.g = (uint16_t)finalColor.g * _masterBrightness / 255;
    finalColor.b = (uint16_t)finalColor.b * _masterBrightness / 255;
  }

  // Ecrire les pixels du segment
  for (uint16_t p = 0; p < seg.pixelCount; p++) {
    RgbColor pixelColor = finalColor;

    // Animation RIPPLE : fade depuis le centre
    if (_hitStates[segIdx].active &&
        seg.hitAnimation == (uint8_t)LedHitAnimation::RIPPLE &&
        seg.pixelCount > 1) {
      uint32_t elapsed = now - _hitStates[segIdx].startMs;
      float progress = (float)elapsed / _hitStates[segIdx].durationMs;
      float center = seg.pixelCount / 2.0f;
      float dist = abs((float)p - center) / center; // 0=centre, 1=bord
      float ripple = 1.0f - abs(progress - dist);
      if (ripple < 0) ripple = 0;

      pixelColor = _blendColors(idleColor, hitColor, (uint8_t)(ripple * hitAlpha));
    }

    _driver.setPixel(seg.stripId, seg.pixelStart + p, pixelColor);
  }
}

// ============================================================================
// Animations
// ============================================================================

RgbColor LedEngine::_computeHitColor(uint8_t segIdx, uint32_t elapsedMs) {
  const LedSegmentConfig& seg = _segments[segIdx];
  RgbColor color = seg.hitColor;

  // Appliquer la brightness hit
  color.r = (uint16_t)color.r * seg.hitBrightness / 255;
  color.g = (uint16_t)color.g * seg.hitBrightness / 255;
  color.b = (uint16_t)color.b * seg.hitBrightness / 255;

  // Animation PULSE : montee puis descente
  if (seg.hitAnimation == (uint8_t)LedHitAnimation::PULSE) {
    float t = (float)elapsedMs / seg.hitFadeDurationMs;
    float intensity;
    if (t < 0.3f) {
      intensity = t / 0.3f; // Montee rapide
    } else {
      intensity = 1.0f - (t - 0.3f) / 0.7f; // Descente lente
    }
    if (intensity < 0) intensity = 0;
    color.r = (uint8_t)(color.r * intensity);
    color.g = (uint8_t)(color.g * intensity);
    color.b = (uint8_t)(color.b * intensity);
  }
  // FLASH : intensite constante, l'alpha gere le fade

  return color;
}

RgbColor LedEngine::_computeIdleColor(uint8_t segIdx, uint32_t nowMs) {
  const LedSegmentConfig& seg = _segments[segIdx];
  uint8_t idleAnim = seg.idleAnimation;

  // Theme override si actif
  LedThemeConfig* theme = getTheme(_activeThemeId);
  if (theme && theme->enabled) {
    // Le theme peut overrider le mode idle
    if (theme->idleMode != (uint8_t)LedIdleAnimation::OFF) {
      idleAnim = theme->idleMode;
    }
  }

  switch ((LedIdleAnimation)idleAnim) {
    case LedIdleAnimation::OFF:
      return RgbColor(0, 0, 0);

    case LedIdleAnimation::STATIC: {
      RgbColor c = seg.idleColor;
      c.r = (uint16_t)c.r * seg.idleBrightness / 255;
      c.g = (uint16_t)c.g * seg.idleBrightness / 255;
      c.b = (uint16_t)c.b * seg.idleBrightness / 255;
      return c;
    }

    case LedIdleAnimation::BREATHING: {
      // Sinusoide lente (periode ~3s)
      float phase = (float)(nowMs % 3000) / 3000.0f;
      float breath = (sin(phase * 2 * PI) + 1.0f) / 2.0f; // 0..1
      uint8_t bri = (uint8_t)(seg.idleBrightness * breath);
      RgbColor c = seg.idleColor;
      c.r = (uint16_t)c.r * bri / 255;
      c.g = (uint16_t)c.g * bri / 255;
      c.b = (uint16_t)c.b * bri / 255;
      return c;
    }

    case LedIdleAnimation::RAINBOW: {
      // Arc-en-ciel defilant (utilise l'index segment pour le decalage)
      uint16_t hue = ((nowMs / 20) + segIdx * 40) % 360;
      // HSV vers RGB simplifie
      uint8_t sector = hue / 60;
      uint8_t remainder = (hue % 60) * 255 / 60;
      uint8_t bri = seg.idleBrightness;
      uint8_t p = 0;
      uint8_t q = (255 - remainder) * bri / 255;
      uint8_t t = remainder * bri / 255;

      switch (sector) {
        case 0:  return RgbColor(bri, t, p);
        case 1:  return RgbColor(q, bri, p);
        case 2:  return RgbColor(p, bri, t);
        case 3:  return RgbColor(p, q, bri);
        case 4:  return RgbColor(t, p, bri);
        default: return RgbColor(bri, p, q);
      }
    }
  }

  return RgbColor(0, 0, 0);
}

RgbColor LedEngine::_blendColors(const RgbColor& idle, const RgbColor& hit, uint8_t hitAlpha) {
  if (hitAlpha == 0) return idle;
  if (hitAlpha == 255) return hit;

  uint8_t invAlpha = 255 - hitAlpha;
  return RgbColor(
    (uint16_t)(idle.r * invAlpha + hit.r * hitAlpha) / 255,
    (uint16_t)(idle.g * invAlpha + hit.g * hitAlpha) / 255,
    (uint16_t)(idle.b * invAlpha + hit.b * hitAlpha) / 255
  );
}

// ============================================================================
// Helpers
// ============================================================================

int8_t LedEngine::_findSegmentIndex(uint8_t id) const {
  for (uint8_t i = 0; i < _segmentCount; i++) {
    if (_segments[i].id == id) return i;
  }
  return -1;
}

int8_t LedEngine::_findThemeIndex(uint8_t id) const {
  for (uint8_t i = 0; i < _themeCount; i++) {
    if (_themes[i].id == id) return i;
  }
  return -1;
}

uint8_t LedEngine::_nextSegmentId() const {
  uint8_t maxId = 0;
  for (uint8_t i = 0; i < _segmentCount; i++) {
    if (_segments[i].id > maxId) maxId = _segments[i].id;
  }
  return maxId + 1;
}

uint8_t LedEngine::_nextThemeId() const {
  uint8_t maxId = 0;
  for (uint8_t i = 0; i < _themeCount; i++) {
    if (_themes[i].id > maxId) maxId = _themes[i].id;
  }
  return maxId + 1;
}
