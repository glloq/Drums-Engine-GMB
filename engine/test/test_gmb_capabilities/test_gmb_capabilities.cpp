// ============================================================================
// Tests natifs de la derivation des capacites GMB
// ============================================================================
// Ce que ces tests protegent, c'est la traduction
//     actionneur -> pipeline -> instrument logique -> note/CC MIDI
// et surtout ses cas ou une implementation naive se trompe : une note derriere
// deux actionneurs annoncee deux fois, un instrument desactive annonce quand
// meme, un canal 10 declare comme 10 alors que le descripteur compte a partir
// de zero, ou une polyphonie egale au nombre d'actionneurs.
#include <unity.h>
#include "gmb_test_fixture.h"
#include "../../src/gmb/gmb_capabilities.cpp"

static GmbFixture fx;
static CapabilitySnapshot snap;

void setUp() { fx.reset(); }
void tearDown() {}

static GmbBuildInputs inputs(uint16_t channelMask = 0xFFFF) {
  GmbBuildInputs in;
  in.lookup = &fx.lookup;
  in.actuatorLookup = GmbFixture::resolve;
  in.actuatorCtx = &fx;
  in.channelMask = channelMask;
  return in;
}

static const GmbInstrumentCaps* instrumentOnChannel(uint8_t zeroBasedChannel) {
  for (uint8_t i = 0; i < snap.instrumentCount; i++) {
    if (snap.instruments[i].channel == zeroBasedChannel) return &snap.instruments[i];
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Kit General MIDI de base
// ---------------------------------------------------------------------------
void test_gm_kit_announces_exactly_the_configured_notes() {
  fx.buildGmKit(10);
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);

  TEST_ASSERT_EQUAL_UINT8(1, snap.instrumentCount);
  const GmbInstrumentCaps& e = snap.instruments[0];
  TEST_ASSERT_EQUAL_UINT8(5, e.notes.count());
  const uint8_t expected[] = {36, 38, 42, 46, 49};
  for (uint8_t i = 0; i < 5; i++) TEST_ASSERT_TRUE(e.notes.has(expected[i]));

  // Et surtout : PAS toute la plage GM 35..81 sous pretexte que c'est un kit.
  TEST_ASSERT_FALSE(e.notes.has(35));
  TEST_ASSERT_FALSE(e.notes.has(37));
  TEST_ASSERT_FALSE(e.notes.has(81));
  TEST_ASSERT_TRUE((e.flags & GMB_INST_CONFIGURED) != 0);
}

void test_user_channel_10_is_declared_as_channel_9() {
  // Le General MIDI parle du canal 10 ; le moteur le stocke 1-based ; le
  // descripteur le declare 0-based. Un decalage d'un ici et GMB configure le
  // mauvais canal — c'est la sorte de bug qui ne se voit qu'a l'usage.
  fx.buildGmKit(10);
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(1, snap.instrumentCount);
  TEST_ASSERT_EQUAL_UINT8(9, snap.instruments[0].channel);
}

void test_channel_one_is_declared_as_channel_zero() {
  fx.buildGmKit(1);
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(1, snap.instrumentCount);
  TEST_ASSERT_EQUAL_UINT8(0, snap.instruments[0].channel);
}

// ---------------------------------------------------------------------------
// Instruments desactives / inutilisables
// ---------------------------------------------------------------------------
void test_pipeline_whose_actuator_is_disabled_is_not_announced() {
  const uint8_t kick = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t snare = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(kick, 8, 25));
  GmbFixture::addNoteOn(fx.addPipeline(10, 38), GmbFixture::strike(snare, 8, 25));
  fx.disableActuator(snare);
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(1, snap.instrumentCount);
  TEST_ASSERT_TRUE(snap.instruments[0].notes.has(36));
  TEST_ASSERT_FALSE(snap.instruments[0].notes.has(38));
}

void test_pipeline_with_no_actions_is_not_announced() {
  const uint8_t kick = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(kick, 8, 25));
  fx.addPipeline(10, 41);     // route, mais ne declenche rien
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(1, snap.instruments[0].notes.count());
  TEST_ASSERT_FALSE(snap.instruments[0].notes.has(41));
}

void test_no_usable_configuration_produces_no_instrument() {
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(0, snap.instrumentCount);
  TEST_ASSERT_EQUAL_UINT8(0, snap.mechanismCount);
}

// ---------------------------------------------------------------------------
// Notes en double
// ---------------------------------------------------------------------------
void test_two_actuators_behind_one_note_announce_the_note_once() {
  // Un tom a double frappe : deux solenoides, une seule note MIDI. Le
  // descripteur decrit des capacites MIDI, pas un inventaire de quincaillerie.
  const uint8_t a = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t b = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  CompiledPipeline& p = fx.addPipeline(10, 38);
  GmbFixture::addNoteOn(p, GmbFixture::strike(a, 8, 22));
  GmbFixture::addNoteOn(p, GmbFixture::strike(b, 8, 22));
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(1, snap.instruments[0].notes.count());
  TEST_ASSERT_TRUE(snap.instruments[0].notes.has(38));
  // Les deux actionneurs restent visibles comme deux voix : c'est la
  // polyphonie qui les compte, pas la liste de notes.
  TEST_ASSERT_EQUAL_UINT8(2, snap.mechanismCount);
}

void test_two_pipelines_claiming_the_same_note_yield_one_note() {
  // Le compilateur en masque un ; le descripteur ne doit pas annoncer deux fois
  // la meme note, ni compter la note masquee comme jouable en double.
  const uint8_t a = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t b = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 38), GmbFixture::strike(a, 8, 22));
  GmbFixture::addNoteOn(fx.addPipeline(10, 38), GmbFixture::strike(b, 8, 22));
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(1, snap.instrumentCount);
  TEST_ASSERT_EQUAL_UINT8(1, snap.instruments[0].notes.count());
  TEST_ASSERT_TRUE(snap.instruments[0].notes.has(38));
}

// ---------------------------------------------------------------------------
// Plusieurs canaux
// ---------------------------------------------------------------------------
void test_two_independent_channels_yield_two_instruments() {
  const uint8_t kick = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t bell = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(kick, 8, 25));
  GmbFixture::addNoteOn(fx.addPipeline(3, 60), GmbFixture::strike(bell, 8, 25));
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(2, snap.instrumentCount);

  const GmbInstrumentCaps* drums = instrumentOnChannel(9);
  const GmbInstrumentCaps* perc = instrumentOnChannel(2);
  TEST_ASSERT_NOT_NULL(drums);
  TEST_ASSERT_NOT_NULL(perc);
  TEST_ASSERT_TRUE(drums->notes.has(36));
  TEST_ASSERT_FALSE(drums->notes.has(60));
  TEST_ASSERT_TRUE(perc->notes.has(60));
  TEST_ASSERT_FALSE(perc->notes.has(36));
}

void test_same_note_on_two_channels_is_announced_on_both() {
  const uint8_t a = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t b = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 38), GmbFixture::strike(a, 8, 25));
  GmbFixture::addNoteOn(fx.addPipeline(11, 38), GmbFixture::strike(b, 8, 25));
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(2, snap.instrumentCount);
  TEST_ASSERT_TRUE(instrumentOnChannel(9)->notes.has(38));
  TEST_ASSERT_TRUE(instrumentOnChannel(10)->notes.has(38));
}

void test_omni_only_configuration_lands_on_the_gm_percussion_channel() {
  // Un instrument OMNI repond sur les 16 canaux. Le declarer 16 fois saturerait
  // le plafond de l'hote avec des entrees identiques : une seule entree, sur le
  // canal de percussion GM.
  const uint8_t kick = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(0, 36), GmbFixture::strike(kick, 8, 25));
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(1, snap.instrumentCount);
  TEST_ASSERT_EQUAL_UINT8(GMB_DEFAULT_CHANNEL, snap.instruments[0].channel);
  TEST_ASSERT_TRUE(snap.instruments[0].notes.has(36));
}

void test_omni_notes_join_the_declared_channels() {
  const uint8_t kick = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t clap = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(kick, 8, 25));
  GmbFixture::addNoteOn(fx.addPipeline(0, 39), GmbFixture::strike(clap, 8, 25));
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(1, snap.instrumentCount);
  TEST_ASSERT_EQUAL_UINT8(9, snap.instruments[0].channel);
  TEST_ASSERT_TRUE(snap.instruments[0].notes.has(36));
  TEST_ASSERT_TRUE(snap.instruments[0].notes.has(39));
}

void test_a_filtered_channel_is_not_a_capability() {
  fx.buildGmKit(10);
  GmbBuildInputs in = inputs(0x0001);   // seul le canal 1 passe le filtre
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(0, snap.instrumentCount);
}

// ---------------------------------------------------------------------------
// CC
// ---------------------------------------------------------------------------
void test_only_routed_ccs_are_announced() {
  fx.buildGmKit(10);
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);

  const GmbInstrumentCaps& e = snap.instruments[0];
  TEST_ASSERT_TRUE(e.ccs.has(4));        // pedale hi-hat, reellement routee
  TEST_ASSERT_EQUAL_UINT8(1, e.ccs.count());
  // Un kit de batterie "pourrait normalement" utiliser CC#7 ou CC#11 : sans
  // route configuree, on n'annonce rien.
  TEST_ASSERT_FALSE(e.ccs.has(7));
  TEST_ASSERT_FALSE(e.ccs.has(11));
  TEST_ASSERT_EQUAL_UINT8(4, e.hihatPedalCc);
}

void test_cc_route_on_another_channel_does_not_leak() {
  const uint8_t kick = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t bell = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t servo = fx.addActuator(ActuatorType::SERVO, ActuatorBehavior::SERVO_POSITION);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(kick, 8, 25));
  GmbFixture::addNoteOn(fx.addPipeline(3, 60), GmbFixture::strike(bell, 8, 25));
  fx.addCcRoute(11, servo, 3);           // uniquement sur le canal 3
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_FALSE(instrumentOnChannel(9)->ccs.has(11));
  TEST_ASSERT_TRUE(instrumentOnChannel(2)->ccs.has(11));
}

void test_omni_cc_route_is_announced_on_every_declared_channel() {
  const uint8_t kick = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t bell = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t servo = fx.addActuator(ActuatorType::SERVO, ActuatorBehavior::SERVO_POSITION);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(kick, 8, 25));
  GmbFixture::addNoteOn(fx.addPipeline(3, 60), GmbFixture::strike(bell, 8, 25));
  fx.addCcRoute(1, servo, 0);            // OMNI
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_TRUE(instrumentOnChannel(9)->ccs.has(1));
  TEST_ASSERT_TRUE(instrumentOnChannel(2)->ccs.has(1));
}

void test_virtual_ccs_become_aftertouch_and_pitch_bend_not_cc125_126() {
  // Le moteur fait passer l'aftertouch et le pitch bend par des CC internes.
  // Les annoncer comme des CC#125/126 ferait croire a GMB que la machine repond
  // a des Controller Change qui n'existent pas sur le fil.
  const uint8_t drum = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t tension = fx.addActuator(ActuatorType::SERVO, ActuatorBehavior::PITCH_BEND);
  GmbFixture::addNoteOn(fx.addPipeline(10, 47), GmbFixture::strike(drum, 10, 30));
  fx.addCcRoute(VIRTUAL_CC_PITCH_BEND, tension, 10);
  fx.addCcRoute(VIRTUAL_CC_AFTERTOUCH, tension, 10);
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  const GmbInstrumentCaps& e = snap.instruments[0];
  TEST_ASSERT_FALSE(e.ccs.has(VIRTUAL_CC_PITCH_BEND));
  TEST_ASSERT_FALSE(e.ccs.has(VIRTUAL_CC_AFTERTOUCH));
  TEST_ASSERT_EQUAL_UINT8(0, e.ccs.count());
  TEST_ASSERT_TRUE((e.flags & GMB_INST_PITCHBEND) != 0);
  TEST_ASSERT_TRUE((e.flags & GMB_INST_AFTERTOUCH) != 0);
}

// ---------------------------------------------------------------------------
// Velocite
// ---------------------------------------------------------------------------
void test_velocity_is_advertised_only_when_it_changes_something() {
  const uint8_t expressive = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(expressive, 8, 25));
  fx.finish();
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_TRUE((snap.instruments[0].flags & GMB_INST_VELOCITY) != 0);

  // Meme montage, mais duree min == duree max : la frappe est toujours la meme,
  // quelle que soit la velocite. Annoncer une expressivite serait faux.
  fx.reset();
  const uint8_t flat = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(flat, 15, 15));
  fx.finish();
  GmbBuildInputs in2 = inputs();
  gmbBuildCapabilities(in2, snap);
  TEST_ASSERT_FALSE((snap.instruments[0].flags & GMB_INST_VELOCITY) != 0);
}

void test_fixed_value_source_is_not_velocity() {
  const uint8_t a = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36),
                        GmbFixture::strike(a, 8, 25, 0, ValueSource::FIXED));
  fx.finish();
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_FALSE((snap.instruments[0].flags & GMB_INST_VELOCITY) != 0);
}

void test_solenoid_hold_ignores_velocity() {
  // SOLENOID_HOLD ecrit paramMin comme PWM d'activation et ignore cmd.value.
  const uint8_t a = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_HOLD, 200, 60);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(a, 0, 0));
  fx.finish();
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_FALSE((snap.instruments[0].flags & GMB_INST_VELOCITY) != 0);
}

// ---------------------------------------------------------------------------
// Polyphonie
// ---------------------------------------------------------------------------
void test_polyphony_is_capped_by_the_number_of_sounding_actuators() {
  const uint8_t a = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t b = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t mute = fx.addActuator(ActuatorType::SERVO, ActuatorBehavior::SERVO_MUTE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(a, 8, 25));
  {
    CompiledPipeline& p = fx.addPipeline(10, 38);
    GmbFixture::addNoteOn(p, GmbFixture::strike(b, 8, 25));
    GmbFixture::addNoteOff(p, GmbFixture::position(mute, 20));
  }
  fx.finish();

  GmbBuildInputs in = inputs();
  in.power.maxConcurrent = 8;
  gmbBuildCapabilities(in, snap);
  // Deux frappeurs, un etouffoir : la polyphonie est 2, pas 3.
  TEST_ASSERT_EQUAL_UINT8(2, snap.instruments[0].polyphonyMax);
}

void test_polyphony_respects_the_concurrent_count_ceiling() {
  for (uint8_t i = 0; i < 6; i++) {
    const uint8_t a = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
    GmbFixture::addNoteOn(fx.addPipeline(10, (uint8_t)(36 + i)), GmbFixture::strike(a, 8, 25));
  }
  fx.finish();

  GmbBuildInputs in = inputs();
  in.power.maxConcurrent = 3;
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(3, snap.instruments[0].polyphonyMax);
  TEST_ASSERT_EQUAL_UINT8(3, snap.globalMaxSimultaneous);
}

void test_polyphony_respects_the_peak_current_budget() {
  // 3 A d'alimentation, trois frappeurs a 2 A, 900 mA et 800 mA : au mieux
  // deux d'entre eux tiennent ensemble (800 + 900 = 1700, +2000 depasse).
  const uint8_t big = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE, 8, 25, 0, 2000);
  const uint8_t mid = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE, 8, 25, 0, 900);
  const uint8_t small = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE, 8, 25, 0, 800);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(big, 8, 25));
  GmbFixture::addNoteOn(fx.addPipeline(10, 38), GmbFixture::strike(mid, 8, 25));
  GmbFixture::addNoteOn(fx.addPipeline(10, 42), GmbFixture::strike(small, 8, 25));
  fx.finish();

  GmbBuildInputs in = inputs();
  in.power.maxConcurrent = 8;
  in.power.maxPeakMa = 3000;
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(2, snap.instruments[0].polyphonyMax);
}

void test_undeclared_current_never_reduces_polyphony() {
  // Un actionneur dont le courant n'est pas renseigne ne consomme pas de budget
  // et n'est jamais refuse : c'est la regle de powerBudgetAdmit().
  for (uint8_t i = 0; i < 4; i++) {
    const uint8_t a = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE, 8, 25, 0, 0);
    GmbFixture::addNoteOn(fx.addPipeline(10, (uint8_t)(36 + i)), GmbFixture::strike(a, 8, 25));
  }
  fx.finish();

  GmbBuildInputs in = inputs();
  in.power.maxConcurrent = 8;
  in.power.maxPeakMa = 100;    // ridiculement bas, mais rien n'est declare
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(4, snap.instruments[0].polyphonyMax);
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
void test_direct_strike_has_no_prepare_phase() {
  fx.buildGmKit(10);
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  const GmbTiming& t = snap.instruments[0].timing;
  TEST_ASSERT_TRUE((t.known & GMB_T_PREPARE) == 0);
  TEST_ASSERT_TRUE((t.known & GMB_T_EXCITE) != 0);
  TEST_ASSERT_EQUAL_UINT16(0, t.exciteLatencyMs);
}

void test_a_positioning_step_before_the_strike_becomes_a_prepare_window() {
  // Un etouffoir qui se retire a t=0 et une frappe a t=40 : les 40 ms sont un
  // geste MECANIQUE ET SILENCIEUX, que GMB peut anticiper. Ils ne doivent pas
  // etre comptes une seconde fois dans la latence d'excitation.
  const uint8_t mute = fx.addActuator(ActuatorType::SERVO, ActuatorBehavior::SERVO_MUTE);
  const uint8_t hammer = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  CompiledPipeline& p = fx.addPipeline(10, 36);
  GmbFixture::addNoteOn(p, GmbFixture::position(mute, 0));
  GmbFixture::addNoteOn(p, GmbFixture::strike(hammer, 8, 25, 40));
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  const GmbTiming& t = snap.instruments[0].timing;
  TEST_ASSERT_TRUE((t.known & GMB_T_PREPARE) != 0);
  TEST_ASSERT_EQUAL_UINT16(40, t.prepareMaxMs);
  TEST_ASSERT_EQUAL_UINT16(0, t.exciteLatencyMs);
}

void test_a_delayed_strike_without_preparation_is_pure_latency() {
  const uint8_t hammer = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(hammer, 8, 25, 12));
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  const GmbTiming& t = snap.instruments[0].timing;
  TEST_ASSERT_TRUE((t.known & GMB_T_PREPARE) == 0);
  TEST_ASSERT_EQUAL_UINT16(12, t.exciteLatencyMs);
}

void test_rearticulation_comes_from_cooldown_and_strike_duration() {
  // cooldownUs est en unites de 100 us : 400 => 40 ms, qui l'emporte sur la
  // duree d'impulsion maximale (25 ms).
  const uint8_t slow = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE, 8, 25, 400);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(slow, 8, 25));
  fx.finish();
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT16(40, snap.instruments[0].timing.rearticulationMs);

  // Sans cooldown, une bobine ne peut pas refrapper avant la fin de son
  // impulsion : c'est la duree d'impulsion qui fait plancher.
  fx.reset();
  const uint8_t noCooldown = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE, 8, 25, 0);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(noCooldown, 8, 30));
  fx.finish();
  GmbBuildInputs in2 = inputs();
  gmbBuildCapabilities(in2, snap);
  TEST_ASSERT_EQUAL_UINT16(30, snap.instruments[0].timing.rearticulationMs);
}

void test_release_is_derived_from_note_off_delays() {
  fx.buildGmKit(10);
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  const GmbTiming& t = snap.instruments[0].timing;
  TEST_ASSERT_TRUE((t.known & GMB_T_RELEASE) != 0);
  TEST_ASSERT_EQUAL_UINT16(50, t.releaseMs);
}

void test_no_note_off_leaves_release_unknown() {
  const uint8_t a = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(a, 8, 25));
  fx.finish();
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_TRUE((snap.instruments[0].timing.known & GMB_T_RELEASE) == 0);
}

void test_jitter_follows_the_scheduler_tick() {
  fx.buildGmKit(10);
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  const uint8_t expected = (SCHEDULER_TICK_HZ >= 1000) ? 1 : (uint8_t)(1000 / SCHEDULER_TICK_HZ);
  TEST_ASSERT_EQUAL_UINT8(expected, snap.instruments[0].timing.jitterMs);
}

// ---------------------------------------------------------------------------
// Roles et mecanismes
// ---------------------------------------------------------------------------
void test_roles_classify_percussion_mechanisms() {
  TEST_ASSERT_TRUE(gmbRoleForBehavior(ActuatorBehavior::SOLENOID_STRIKE) == GmbRole::STRIKE);
  TEST_ASSERT_TRUE(gmbRoleForBehavior(ActuatorBehavior::MOTOR_OPTICAL_TRACK) == GmbRole::STRIKE);
  TEST_ASSERT_TRUE(gmbRoleForBehavior(ActuatorBehavior::STEPPER_STRIKE) == GmbRole::STRIKE);
  TEST_ASSERT_TRUE(gmbRoleForBehavior(ActuatorBehavior::SERVO_MUTE) == GmbRole::DAMP);
  TEST_ASSERT_TRUE(gmbRoleForBehavior(ActuatorBehavior::SOLENOID_MUTE) == GmbRole::DAMP);
  TEST_ASSERT_TRUE(gmbRoleForBehavior(ActuatorBehavior::SERVO_POSITION) == GmbRole::DAMP);
  TEST_ASSERT_TRUE(gmbRoleForBehavior(ActuatorBehavior::HIHAT_CONTROLLER) == GmbRole::PEDAL);
  TEST_ASSERT_TRUE(gmbRoleForBehavior(ActuatorBehavior::PITCH_BEND) == GmbRole::TUNE);
  TEST_ASSERT_TRUE(gmbRoleForBehavior(ActuatorBehavior::STEPPER_POSITION) == GmbRole::POSITION);
}

void test_a_shared_damper_groups_the_notes_it_chokes() {
  const uint8_t crash = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t ride = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t mute = fx.addActuator(ActuatorType::SERVO, ActuatorBehavior::SERVO_MUTE);
  {
    CompiledPipeline& p = fx.addPipeline(10, 49);
    GmbFixture::addNoteOn(p, GmbFixture::strike(crash, 8, 20));
    GmbFixture::addNoteOff(p, GmbFixture::position(mute, 30));
  }
  {
    CompiledPipeline& p = fx.addPipeline(10, 51);
    GmbFixture::addNoteOn(p, GmbFixture::strike(ride, 8, 20));
    GmbFixture::addNoteOff(p, GmbFixture::position(mute, 30));
  }
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);

  const GmbMechanism* damper = nullptr;
  for (uint8_t i = 0; i < snap.mechanismCount; i++) {
    if ((GmbRole)snap.mechanisms[i].role == GmbRole::DAMP) damper = &snap.mechanisms[i];
  }
  TEST_ASSERT_NOT_NULL(damper);
  // Les deux cymbales partagent un etouffoir : c'est exactement une relation de
  // choke, et elle se lit dans la configuration sans qu'on ait a la saisir.
  TEST_ASSERT_TRUE(damper->notes.has(49));
  TEST_ASSERT_TRUE(damper->notes.has(51));
  TEST_ASSERT_EQUAL_UINT8(2, damper->notes.count());
}

void test_a_shared_striker_lists_every_note_it_serves() {
  fx.buildGmKit(10);
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);

  // Le charleston ferme (42) et ouvert (46) sortent du meme frappeur.
  const GmbMechanism* hh = nullptr;
  for (uint8_t i = 0; i < snap.mechanismCount; i++) {
    if (snap.mechanisms[i].notes.has(42)) hh = &snap.mechanisms[i];
  }
  TEST_ASSERT_NOT_NULL(hh);
  TEST_ASSERT_TRUE(hh->notes.has(46));
  TEST_ASSERT_EQUAL_UINT8(2, hh->notes.count());
}

// ---------------------------------------------------------------------------
// Determinisme
// ---------------------------------------------------------------------------
void test_the_same_configuration_always_yields_the_same_snapshot() {
  // La detection de changement de capacites repose entierement sur ce point.
  fx.buildGmKit(10);
  CapabilitySnapshot a, b;
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, a);
  gmbBuildCapabilities(in, b);
  TEST_ASSERT_EQUAL_UINT8(a.instrumentCount, b.instrumentCount);
  TEST_ASSERT_EQUAL_UINT8(a.mechanismCount, b.mechanismCount);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(CapabilitySnapshot));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_gm_kit_announces_exactly_the_configured_notes);
  RUN_TEST(test_user_channel_10_is_declared_as_channel_9);
  RUN_TEST(test_channel_one_is_declared_as_channel_zero);
  RUN_TEST(test_pipeline_whose_actuator_is_disabled_is_not_announced);
  RUN_TEST(test_pipeline_with_no_actions_is_not_announced);
  RUN_TEST(test_no_usable_configuration_produces_no_instrument);
  RUN_TEST(test_two_actuators_behind_one_note_announce_the_note_once);
  RUN_TEST(test_two_pipelines_claiming_the_same_note_yield_one_note);
  RUN_TEST(test_two_independent_channels_yield_two_instruments);
  RUN_TEST(test_same_note_on_two_channels_is_announced_on_both);
  RUN_TEST(test_omni_only_configuration_lands_on_the_gm_percussion_channel);
  RUN_TEST(test_omni_notes_join_the_declared_channels);
  RUN_TEST(test_a_filtered_channel_is_not_a_capability);
  RUN_TEST(test_only_routed_ccs_are_announced);
  RUN_TEST(test_cc_route_on_another_channel_does_not_leak);
  RUN_TEST(test_omni_cc_route_is_announced_on_every_declared_channel);
  RUN_TEST(test_virtual_ccs_become_aftertouch_and_pitch_bend_not_cc125_126);
  RUN_TEST(test_velocity_is_advertised_only_when_it_changes_something);
  RUN_TEST(test_fixed_value_source_is_not_velocity);
  RUN_TEST(test_solenoid_hold_ignores_velocity);
  RUN_TEST(test_polyphony_is_capped_by_the_number_of_sounding_actuators);
  RUN_TEST(test_polyphony_respects_the_concurrent_count_ceiling);
  RUN_TEST(test_polyphony_respects_the_peak_current_budget);
  RUN_TEST(test_undeclared_current_never_reduces_polyphony);
  RUN_TEST(test_direct_strike_has_no_prepare_phase);
  RUN_TEST(test_a_positioning_step_before_the_strike_becomes_a_prepare_window);
  RUN_TEST(test_a_delayed_strike_without_preparation_is_pure_latency);
  RUN_TEST(test_rearticulation_comes_from_cooldown_and_strike_duration);
  RUN_TEST(test_release_is_derived_from_note_off_delays);
  RUN_TEST(test_no_note_off_leaves_release_unknown);
  RUN_TEST(test_jitter_follows_the_scheduler_tick);
  RUN_TEST(test_roles_classify_percussion_mechanisms);
  RUN_TEST(test_a_shared_damper_groups_the_notes_it_chokes);
  RUN_TEST(test_a_shared_striker_lists_every_note_it_serves);
  RUN_TEST(test_the_same_configuration_always_yields_the_same_snapshot);
  return UNITY_END();
}
