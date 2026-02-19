#ifndef LED_ENGINE_H
#define LED_ENGINE_H

#include "../core/config.h"
#include "../core/types.h"
#include "led_strip_driver.h"
#include "led_event_queue.h"

// ============================================================================
// LedEngine - Coordinateur LED (tourne sur Core 0 a 60 fps)
// ============================================================================
// Architecture hybride :
//   - Hit animations : declenchees par events MIDI via LedEventQueue
//   - Idle/theme animations : continues en fond
//   - Le hit a priorite sur l'idle (blend alpha)
//
// Pipeline de rendu (chaque frame) :
//   1. Poll LedEventQueue (events MIDI du Core 1)
//   2. Mettre a jour les animations hit actives (fade out)
//   3. Calculer les couleurs idle/theme pour chaque segment
//   4. Blend hit + idle → couleur finale
//   5. Ecrire les pixels et appeler show()
// ============================================================================

class LedEngine {
public:
  LedEngine();

  // Initialiser le moteur LED
  bool begin();

  // Boucle principale (appeler depuis appCoreTask, ~60 fps)
  void update();

  // --- Configuration strips ---
  bool configureStrip(uint8_t stripIdx, const LedStripConfig& config);
  bool removeStrip(uint8_t stripIdx);
  const LedStripConfig& getStripConfig(uint8_t idx) const { return _stripConfigs[idx]; }
  uint8_t getActiveStripCount() const;

  // --- Configuration segments ---
  int addSegment(const LedSegmentConfig& config);
  bool updateSegment(uint8_t id, const LedSegmentConfig& config);
  bool removeSegment(uint8_t id);
  LedSegmentConfig* getSegment(uint8_t id);
  LedSegmentConfig* getSegmentByIndex(uint8_t index);
  uint8_t getSegmentCount() const { return _segmentCount; }

  // --- Themes ---
  int addTheme(const LedThemeConfig& config);
  bool updateTheme(uint8_t id, const LedThemeConfig& config);
  bool removeTheme(uint8_t id);
  LedThemeConfig* getTheme(uint8_t id);
  LedThemeConfig* getThemeByIndex(uint8_t index);
  uint8_t getThemeCount() const { return _themeCount; }
  void setActiveTheme(uint8_t themeId);
  uint8_t getActiveThemeId() const { return _activeThemeId; }

  // --- Luminosite master ---
  void setMasterBrightness(uint8_t brightness);
  uint8_t getMasterBrightness() const { return _masterBrightness; }

  // --- Event queue (expose pour EventProcessor sur Core 1) ---
  LedEventQueue& getEventQueue() { return _eventQueue; }

  // --- Test ---
  void testSegment(uint8_t segmentId, uint8_t brightness = 255);
  void testStrip(uint8_t stripIdx);

  // --- Lookup note MIDI → segment(s) ---
  void rebuildNoteLookup();
  void registerNoteSegment(uint8_t midiNote, uint8_t segmentIndex);

private:
  LedStripDriver _driver;
  LedEventQueue _eventQueue;

  // Configuration
  LedStripConfig _stripConfigs[LED_MAX_STRIPS];
  LedSegmentConfig _segments[LED_MAX_SEGMENTS];
  uint8_t _segmentCount;
  LedThemeConfig _themes[LED_MAX_THEMES];
  uint8_t _themeCount;
  uint8_t _activeThemeId;
  uint8_t _masterBrightness;

  // Etat animation hit par segment
  struct HitState {
    bool active;
    uint8_t velocity;
    uint32_t startMs;      // millis() au declenchement
    uint16_t durationMs;   // Duree fade configuree
  };
  HitState _hitStates[LED_MAX_SEGMENTS];

  // Lookup note MIDI → segments (un instrument peut avoir plusieurs segments)
  static constexpr uint8_t MAX_SEGMENTS_PER_NOTE = 4;
  struct NoteLookup {
    uint8_t segmentIndices[MAX_SEGMENTS_PER_NOTE];
    uint8_t count;
  };
  NoteLookup _noteLookup[128];

  // Timing
  uint32_t _lastFrameMs;
  uint32_t _frameCount;

  // --- Rendu ---
  void _processEvents();
  void _renderFrame();
  void _renderSegment(uint8_t segIdx);

  // --- Animations ---
  RgbColor _computeHitColor(uint8_t segIdx, uint32_t elapsedMs);
  RgbColor _computeIdleColor(uint8_t segIdx, uint32_t nowMs);
  RgbColor _blendColors(const RgbColor& idle, const RgbColor& hit, uint8_t hitAlpha);

  // --- Helpers ---
  int8_t _findSegmentIndex(uint8_t id) const;
  int8_t _findThemeIndex(uint8_t id) const;
  uint8_t _nextSegmentId() const;
  uint8_t _nextThemeId() const;
};

#endif // LED_ENGINE_H
