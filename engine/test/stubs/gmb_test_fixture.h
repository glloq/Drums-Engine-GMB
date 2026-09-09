#pragma once
// ============================================================================
// Fixture partagee des tests GMB
// ============================================================================
// Deux binaires de test (test_gmb_capabilities et test_gmb_service) partent de
// la meme chose : une PipelineLookup COMPILEE et une table d'actionneurs. La
// fixture vit ici parce que test/stubs est le seul repertoire que
// platformio.ini met sur le chemin d'inclusion de tous les tests natifs ;
// dupliquer ce montage dans chaque test le ferait deriver, et c'est justement
// la derive entre deux descriptions d'une meme chose que ce depot passe son
// temps a corriger.
//
// La table (canal, note) est construite avec midiBuildNoteMap(), exactement
// comme PipelineCompiler::buildNoteMap() sur la cible : les tests heritent donc
// des VRAIES regles de precedence OMNI / canal explicite, sans les reimplementer.
#include "../../src/core/types.h"
#include "../../src/core/midi_routing.h"

struct GmbFixture {
  ActuatorConfig actuators[MAX_ACTUATORS];
  uint8_t actuatorCount = 0;
  PipelineLookup lookup;

  void reset() {
    actuatorCount = 0;
    for (uint8_t i = 0; i < MAX_ACTUATORS; i++) actuators[i] = ActuatorConfig();
    lookup = PipelineLookup();
  }

  // Ajoute un actionneur et renvoie son id.
  uint8_t addActuator(ActuatorType type, ActuatorBehavior behavior,
                      uint16_t paramMin = 8, uint16_t paramMax = 25,
                      uint16_t cooldownUnits = 0, uint16_t currentMa = 0,
                      bool enabled = true) {
    ActuatorConfig& c = actuators[actuatorCount];
    c = ActuatorConfig();
    c.id = actuatorCount;
    c.type = type;
    c.behavior = behavior;
    c.paramMin = paramMin;
    c.paramMax = paramMax;
    c.cooldownUs = cooldownUnits;   // unites de 100 us, comme sur la cible
    c.currentMa = currentMa;
    c.enabled = enabled;
    return actuatorCount++;
  }

  void disableActuator(uint8_t id) {
    for (uint8_t i = 0; i < actuatorCount; i++) {
      if (actuators[i].id == id) actuators[i].enabled = false;
    }
  }

  // `channel` est le canal du fil (1..16), 0 = OMNI — la convention interne du
  // moteur. Le descripteur, lui, declare des canaux 0-based : c'est justement
  // la conversion que les tests verifient.
  CompiledPipeline& addPipeline(uint8_t channel, uint8_t note) {
    CompiledPipeline& p = lookup.pipelines[lookup.pipeline_count++];
    p = CompiledPipeline();
    p.midi_channel = channel;
    p.midi_note = note;
    return p;
  }

  static ActionStep strike(uint8_t actuatorId, uint16_t durMinMs, uint16_t durMaxMs,
                           uint16_t delayMs = 0,
                           ValueSource src = ValueSource::VELOCITY) {
    ActionStep s;
    s.actuator_id = actuatorId;
    s.command_type = (uint8_t)CommandType::PULSE;
    s.value_source = (uint8_t)src;
    s.duration_min_ms = durMinMs;
    s.duration_max_ms = durMaxMs;
    s.delay_ms = delayMs;
    return s;
  }

  static ActionStep position(uint8_t actuatorId, uint16_t delayMs = 0,
                             ValueSource src = ValueSource::FIXED,
                             uint8_t fixed = 0) {
    ActionStep s;
    s.actuator_id = actuatorId;
    s.command_type = (uint8_t)CommandType::POSITION;
    s.value_source = (uint8_t)src;
    s.value_fixed = fixed;
    s.delay_ms = delayMs;
    return s;
  }

  static ActionStep off(uint8_t actuatorId, uint16_t delayMs = 0) {
    ActionStep s;
    s.actuator_id = actuatorId;
    s.command_type = (uint8_t)CommandType::OFF;
    s.value_source = (uint8_t)ValueSource::FIXED;
    s.delay_ms = delayMs;
    return s;
  }

  static void addNoteOn(CompiledPipeline& p, const ActionStep& s) {
    if (p.note_on_count < MAX_ACTIONS_PER_EVENT) p.note_on_actions[p.note_on_count++] = s;
  }
  static void addNoteOff(CompiledPipeline& p, const ActionStep& s) {
    if (p.note_off_count < MAX_ACTIONS_PER_EVENT) p.note_off_actions[p.note_off_count++] = s;
  }

  // Une route CC telle que compilePipelines() la produit : `channel` 0 = OMNI.
  void addCcRoute(uint8_t cc, uint8_t actuatorId, uint8_t channel) {
    const uint8_t idx = lookup.cc_route_count;
    CCRoutingEntry& r = lookup.cc_routes[idx];
    r = CCRoutingEntry();
    r.actuator_id = actuatorId;
    r.command_type = (uint8_t)CommandType::POSITION;
    r.channel = channel;
    lookup.cc_route_count++;
    if (lookup.cc_to_first[cc] == 0xFF) lookup.cc_to_first[cc] = idx;
    lookup.cc_to_count[cc]++;
  }

  // Construire la table (canal, note). Memes regles que sur la cible.
  void finish() {
    midiBuildNoteMap<uint8_t>(
        lookup.note_to_pipeline, 0xFF, lookup.pipeline_count,
        [this](uint16_t i) -> MidiBinding {
          const CompiledPipeline& pipe = lookup.pipelines[i];
          return { pipe.midi_channel, pipe.midi_note, pipe.midi_note < 128 };
        });
  }

  // Resolveur d'actionneur au format attendu par GmbBuildInputs.
  static const ActuatorConfig* resolve(void* ctx, uint8_t actuatorId) {
    GmbFixture* f = (GmbFixture*)ctx;
    for (uint8_t i = 0; i < f->actuatorCount; i++) {
      if (f->actuators[i].id == actuatorId) return &f->actuators[i];
    }
    return nullptr;
  }

  // Un kit General MIDI reduit : kick 36, caisse claire 38, charleston ferme 42
  // et ouvert 46 (meme actionneur), crash 49 avec etouffoir sur le NoteOff.
  void buildGmKit(uint8_t channel = 10) {
    const uint8_t kick  = addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE, 8, 25, 200, 2000);
    const uint8_t snare = addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE, 6, 20, 150, 1200);
    const uint8_t hh    = addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE, 8, 25, 200, 900);
    const uint8_t pedal = addActuator(ActuatorType::SERVO,    ActuatorBehavior::HIHAT_CONTROLLER, 30, 150, 0, 250);
    const uint8_t crash = addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE, 8, 20, 300, 800);
    const uint8_t mute  = addActuator(ActuatorType::SERVO,    ActuatorBehavior::SERVO_POSITION, 0, 180, 0, 250);

    addNoteOn(addPipeline(channel, 36), strike(kick, 8, 25));
    addNoteOn(addPipeline(channel, 38), strike(snare, 6, 20));
    {
      CompiledPipeline& p = addPipeline(channel, 42);
      addNoteOn(p, strike(hh, 8, 25));
      addNoteOn(p, position(pedal, 0, ValueSource::CC_VAR));
    }
    {
      CompiledPipeline& p = addPipeline(channel, 46);
      addNoteOn(p, strike(hh, 8, 25));
      addNoteOn(p, position(pedal, 0, ValueSource::CC_VAR));
    }
    {
      CompiledPipeline& p = addPipeline(channel, 49);
      addNoteOn(p, strike(crash, 8, 20));
      addNoteOff(p, position(mute, 50));
    }
    addCcRoute(4, pedal, channel);
    finish();
  }
};
