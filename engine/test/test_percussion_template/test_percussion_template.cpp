// ============================================================================
// Native unit tests for the percussion template table
// ============================================================================
// Regression cover for the divergence these tests were written against: a
// template used to be described in THREE places (the GET listing, the slot
// check, the builder), and they had drifted. The tests below assert the two
// properties that catch that class of bug:
//   1. what a template ADVERTISES is what creation ACCEPTS;
//   2. what a template BUILDS passes the instrument validator.
#include <unity.h>
#include "../../src/instrument/percussion_template.cpp"
#include "../../src/instrument/instrument_validation.cpp"
#include "../../src/actuator/actuator_validation.cpp"
#include "../../src/actuator/actuator_descriptor.cpp"

// Actuator table built to satisfy whatever slots a template asks for.
static ActuatorConfig g_acts[MAX_TEMPLATE_SLOTS];
static uint8_t g_actCount = 0;

static const ActuatorConfig* mockLookup(void* ctx, uint8_t id) {
  (void)ctx;
  for (uint8_t i = 0; i < g_actCount; i++) {
    if (g_acts[i].id == id) return &g_acts[i];
  }
  return nullptr;
}

// Build one actuator per declared slot, honouring the template's OWN
// requirements. If a template advertises something creation would refuse, these
// actuators are exactly what the user would wire up — and the check fails.
static void seedActuatorsFor(const PercussionTemplate& t) {
  g_actCount = t.slotCount;
  for (uint8_t s = 0; s < t.slotCount; s++) {
    ActuatorConfig& a = g_acts[s];
    a = ActuatorConfig();
    a.id = (uint8_t)(s + 1);
    a.type = t.slots[s].type;
    if (t.slots[s].behavior != ANY_BEHAVIOR) {
      a.behavior = t.slots[s].behavior;
    } else {
      // "Any behavior": pick the first one the descriptor table lists for the
      // type, which is what an UI generated from /api/capabilities would offer.
      for (uint8_t bi = 0; bi < actuatorBehaviorDescriptorCount(); bi++) {
        const BehaviorDescriptor& d = actuatorBehaviorDescriptors()[bi];
        if (d.type == t.slots[s].type) { a.behavior = d.behavior; break; }
      }
    }
  }
}

void setUp() { g_actCount = 0; }
void tearDown() {}

// --- Coherence de la table ------------------------------------------------

void test_table_is_not_empty_and_ids_are_unique() {
  TEST_ASSERT_TRUE(percussionTemplateCount() > 0);
  for (uint8_t i = 0; i < percussionTemplateCount(); i++) {
    const PercussionTemplate& a = percussionTemplates()[i];
    TEST_ASSERT_NOT_NULL(a.id);
    TEST_ASSERT_NOT_NULL(a.build);
    TEST_ASSERT_TRUE(a.slotCount > 0);
    TEST_ASSERT_TRUE(a.slotCount <= MAX_TEMPLATE_SLOTS);
    for (uint8_t j = i + 1; j < percussionTemplateCount(); j++) {
      TEST_ASSERT_TRUE(strcmp(a.id, percussionTemplates()[j].id) != 0);
    }
  }
}

void test_lookup_by_id() {
  TEST_ASSERT_NOT_NULL(findPercussionTemplate("kick"));
  TEST_ASSERT_NOT_NULL(findPercussionTemplate("hihat"));
  TEST_ASSERT_NULL(findPercussionTemplate("does-not-exist"));
  TEST_ASSERT_NULL(findPercussionTemplate(""));
  TEST_ASSERT_NULL(findPercussionTemplate(nullptr));
}

// Chaque emplacement doit demander un couple type/comportement que le
// validateur d'actionneur reconnait — sinon le modele exige l'impossible.
void test_every_slot_requirement_is_a_real_type_behavior_pair() {
  for (uint8_t i = 0; i < percussionTemplateCount(); i++) {
    const PercussionTemplate& t = percussionTemplates()[i];
    for (uint8_t s = 0; s < t.slotCount; s++) {
      TEST_ASSERT_TRUE((uint8_t)t.slots[s].type < (uint8_t)ActuatorType::TYPE_COUNT);
      if (t.slots[s].behavior == ANY_BEHAVIOR) continue;
      TEST_ASSERT_TRUE(actuatorBehaviorBelongsTo(t.slots[s].behavior, t.slots[s].type));
    }
  }
}

void test_default_notes_and_channels_are_in_range() {
  for (uint8_t i = 0; i < percussionTemplateCount(); i++) {
    const PercussionTemplate& t = percussionTemplates()[i];
    TEST_ASSERT_TRUE(t.defaultNote < 128);
    TEST_ASSERT_TRUE(t.defaultChannel >= 1 && t.defaultChannel <= 16);
  }
}

// --- La propriete centrale ------------------------------------------------

// Ce que le modele ANNONCE doit produire un instrument que le moteur ACCEPTE.
// C'est le test qui aurait attrape le hi-hat : il annoncait un servo
// HIHAT_CONTROLLER pendant que la creation exigeait SERVO_POSITION.
void test_every_template_builds_a_valid_instrument_from_its_own_slots() {
  for (uint8_t i = 0; i < percussionTemplateCount(); i++) {
    const PercussionTemplate& t = percussionTemplates()[i];
    seedActuatorsFor(t);

    uint8_t ids[MAX_TEMPLATE_SLOTS];
    memset(ids, 0xFF, sizeof(ids));
    for (uint8_t s = 0; s < t.slotCount; s++) ids[s] = g_acts[s].id;

    InstrumentConfig inst;
    strlcpy(inst.name, t.name, sizeof(inst.name));
    strlcpy(inst.category, t.category, sizeof(inst.category));
    inst.midiNote = t.defaultNote;
    inst.midiChannel = t.defaultChannel;
    inst.enabled = true;
    t.build(inst, ids);

    const char* err = nullptr;
    if (!validateInstrumentConfig(inst, err, mockLookup, nullptr)) {
      // Nommer le coupable : sinon un echec ne dit pas QUEL modele est casse.
      printf("  template '%s' rejected: %s\n", t.id, err ? err : "?");
      TEST_ASSERT_TRUE(false);
    }
    // Un modele qui ne produit aucune action ne joue rien.
    TEST_ASSERT_TRUE(inst.noteOnCount > 0);
  }
}

// Chaque action/liaison construite doit viser un actionneur reellement demande
// par le modele : viser un emplacement non declare donnerait un instrument
// silencieux.
void test_built_actions_only_reference_declared_slots() {
  for (uint8_t i = 0; i < percussionTemplateCount(); i++) {
    const PercussionTemplate& t = percussionTemplates()[i];
    seedActuatorsFor(t);

    uint8_t ids[MAX_TEMPLATE_SLOTS];
    memset(ids, 0xFF, sizeof(ids));
    for (uint8_t s = 0; s < t.slotCount; s++) ids[s] = g_acts[s].id;

    InstrumentConfig inst;
    inst.midiNote = t.defaultNote;
    inst.midiChannel = t.defaultChannel;
    t.build(inst, ids);

    auto declared = [&](uint8_t id) {
      for (uint8_t s = 0; s < t.slotCount; s++) if (ids[s] == id) return true;
      return false;
    };
    for (uint8_t a = 0; a < inst.noteOnCount; a++) {
      TEST_ASSERT_TRUE(declared(inst.noteOnActions[a].actuator_id));
    }
    for (uint8_t a = 0; a < inst.noteOffCount; a++) {
      TEST_ASSERT_TRUE(declared(inst.noteOffActions[a].actuator_id));
    }
    for (uint8_t b = 0; b < inst.ccBindingCount; b++) {
      TEST_ASSERT_TRUE(declared(inst.ccBindings[b].actuator_id));
    }
  }
}

// --- Regressions nommees --------------------------------------------------

// Le hi-hat doit exiger le comportement qui porte reellement la logique CC#4 +
// splash. La creation exigeait SERVO_POSITION, donc suivre l'exigence publiee
// (HIHAT_CONTROLLER) faisait echouer la creation avec un 400.
void test_hihat_requires_the_hihat_controller_behavior() {
  const PercussionTemplate* t = findPercussionTemplate("hihat");
  TEST_ASSERT_NOT_NULL(t);
  TEST_ASSERT_EQUAL_UINT8(2, t->slotCount);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)ActuatorType::SERVO, (uint8_t)t->slots[1].type);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)ActuatorBehavior::HIHAT_CONTROLLER,
                          (uint8_t)t->slots[1].behavior);
  TEST_ASSERT_EQUAL_UINT8(42, t->defaultNote);   // Closed Hi-Hat, pas 36
}

// La timbale liait son pitch au CC#127. Le moteur convertit le Pitch Bend vers
// VIRTUAL_CC_PITCH_BEND (126) — 127 est le vrai "Poly Mode On" — donc la
// liaison ne recevait jamais rien.
void test_timpani_binds_pitch_to_the_virtual_pitch_bend_cc() {
  const PercussionTemplate* t = findPercussionTemplate("timpani");
  TEST_ASSERT_NOT_NULL(t);
  seedActuatorsFor(*t);

  uint8_t ids[MAX_TEMPLATE_SLOTS] = { g_acts[0].id, g_acts[1].id, 0xFF };
  InstrumentConfig inst;
  inst.midiNote = t->defaultNote;
  t->build(inst, ids);

  TEST_ASSERT_EQUAL_UINT8(1, inst.ccBindingCount);
  TEST_ASSERT_EQUAL_UINT8(VIRTUAL_CC_PITCH_BEND, inst.ccBindings[0].cc_number);
}

// Chaque modele porte SA note ; la creation partait d'un 36 code en dur, donc
// un modele cree sans note explicite atterrissait sur la grosse caisse.
void test_templates_carry_distinct_default_notes() {
  TEST_ASSERT_EQUAL_UINT8(36, findPercussionTemplate("kick")->defaultNote);
  TEST_ASSERT_EQUAL_UINT8(42, findPercussionTemplate("hihat")->defaultNote);
  TEST_ASSERT_EQUAL_UINT8(49, findPercussionTemplate("cymbal_mute")->defaultNote);
  TEST_ASSERT_EQUAL_UINT8(45, findPercussionTemplate("double_hit")->defaultNote);
  TEST_ASSERT_EQUAL_UINT8(82, findPercussionTemplate("shaker")->defaultNote);
  TEST_ASSERT_EQUAL_UINT8(38, findPercussionTemplate("brush")->defaultNote);
  TEST_ASSERT_EQUAL_UINT8(47, findPercussionTemplate("timpani")->defaultNote);
  TEST_ASSERT_EQUAL_UINT8(60, findPercussionTemplate("motor_optical")->defaultNote);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_table_is_not_empty_and_ids_are_unique);
  RUN_TEST(test_lookup_by_id);
  RUN_TEST(test_every_slot_requirement_is_a_real_type_behavior_pair);
  RUN_TEST(test_default_notes_and_channels_are_in_range);
  RUN_TEST(test_every_template_builds_a_valid_instrument_from_its_own_slots);
  RUN_TEST(test_built_actions_only_reference_declared_slots);
  RUN_TEST(test_hihat_requires_the_hihat_controller_behavior);
  RUN_TEST(test_timpani_binds_pitch_to_the_virtual_pitch_bend_cc);
  RUN_TEST(test_templates_carry_distinct_default_notes);
  return UNITY_END();
}
