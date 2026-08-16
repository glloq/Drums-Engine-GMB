// ============================================================================
// Native unit tests for the shared instrument validator (review #14)
// ============================================================================
#include <unity.h>
#include "../../src/instrument/instrument_validation.cpp"

// Mock actuator table: id 1 = solenoid strike, id 2 = servo position,
// id 3 = stepper position, id 4 = stepper strike, id 5 = stepper rotate.
static ActuatorConfig g_acts[5];

static const ActuatorConfig* mockLookup(void* ctx, uint8_t id) {
  (void)ctx;
  for (auto& a : g_acts) if (a.id == id) return &a;
  return nullptr;
}

static void seedActuators() {
  g_acts[0] = ActuatorConfig();
  g_acts[0].id = 1; g_acts[0].type = ActuatorType::SOLENOID;
  g_acts[0].behavior = ActuatorBehavior::SOLENOID_STRIKE;
  g_acts[1] = ActuatorConfig();
  g_acts[1].id = 2; g_acts[1].type = ActuatorType::SERVO;
  g_acts[1].behavior = ActuatorBehavior::SERVO_POSITION;
  g_acts[2] = ActuatorConfig();
  g_acts[2].id = 3; g_acts[2].type = ActuatorType::STEPPER;
  g_acts[2].behavior = ActuatorBehavior::STEPPER_POSITION;
  g_acts[3] = ActuatorConfig();
  g_acts[3].id = 4; g_acts[3].type = ActuatorType::STEPPER;
  g_acts[3].behavior = ActuatorBehavior::STEPPER_STRIKE;
  g_acts[4] = ActuatorConfig();
  g_acts[4].id = 5; g_acts[4].type = ActuatorType::STEPPER;
  g_acts[4].behavior = ActuatorBehavior::STEPPER_ROTATE;
}

// A stepper accepts all three commands, but each one only makes sense for one
// behavior — sending the wrong one would silently do nothing at runtime.
static bool stepAccepts(uint8_t actuatorId, CommandType cmd) {
  InstrumentConfig inst;
  inst.midiNote = 38; inst.midiChannel = 10;
  inst.noteOnCount = 1;
  inst.noteOnActions[0].actuator_id = actuatorId;
  inst.noteOnActions[0].command_type = (uint8_t)cmd;
  inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
  const char* err = nullptr;
  return validateInstrumentConfig(inst, err, mockLookup, nullptr);
}

static InstrumentConfig baseInstrument() {
  InstrumentConfig inst;
  inst.midiNote = 38; inst.midiChannel = 10;
  inst.noteOnCount = 1;
  inst.noteOnActions[0].actuator_id = 1;
  inst.noteOnActions[0].command_type = (uint8_t)CommandType::PULSE;   // solenoid OK
  inst.noteOnActions[0].value_source = (uint8_t)ValueSource::VELOCITY;
  return inst;
}

void setUp() { seedActuators(); }
void tearDown() {}

void test_valid_instrument_passes() {
  InstrumentConfig inst = baseInstrument();
  const char* err = nullptr;
  TEST_ASSERT_TRUE(validateInstrumentConfig(inst, err, mockLookup, nullptr));
}

void test_note_out_of_range_rejected() {
  InstrumentConfig inst = baseInstrument();
  inst.midiNote = 200;
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateInstrumentConfig(inst, err, mockLookup, nullptr));
}

void test_channel_out_of_range_rejected() {
  InstrumentConfig inst = baseInstrument();
  inst.midiChannel = 17;
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateInstrumentConfig(inst, err, mockLookup, nullptr));
}

void test_unknown_actuator_rejected() {
  InstrumentConfig inst = baseInstrument();
  inst.noteOnActions[0].actuator_id = 99;   // not in table
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateInstrumentConfig(inst, err, mockLookup, nullptr));
}

void test_incompatible_command_rejected() {
  InstrumentConfig inst = baseInstrument();
  // POSITION on a solenoid is incompatible.
  inst.noteOnActions[0].command_type = (uint8_t)CommandType::POSITION;
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateInstrumentConfig(inst, err, mockLookup, nullptr));
}

void test_cc_binding_command_must_be_position_or_pwm() {
  InstrumentConfig inst = baseInstrument();
  inst.ccBindingCount = 1;
  inst.ccBindings[0].cc_number = 1;
  inst.ccBindings[0].actuator_id = 2;                         // servo
  inst.ccBindings[0].command_type = (uint8_t)CommandType::PULSE;  // invalid for CC
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateInstrumentConfig(inst, err, mockLookup, nullptr));

  inst.ccBindings[0].command_type = (uint8_t)CommandType::POSITION;  // valid
  TEST_ASSERT_TRUE(validateInstrumentConfig(inst, err, mockLookup, nullptr));
}

void test_range_normalization() {
  InstrumentConfig inst = baseInstrument();
  inst.ccBindingCount = 1;
  inst.ccBindings[0].cc_number = 7;
  inst.ccBindings[0].actuator_id = 2;
  inst.ccBindings[0].command_type = (uint8_t)CommandType::POSITION;
  inst.ccBindings[0].range_min = 100;
  inst.ccBindings[0].range_max = 20;   // inverted
  const char* err = nullptr;
  TEST_ASSERT_TRUE(validateInstrumentConfig(inst, err, mockLookup, nullptr));
  TEST_ASSERT_EQUAL_UINT8(20, inst.ccBindings[0].range_min);   // swapped
  TEST_ASSERT_EQUAL_UINT8(100, inst.ccBindings[0].range_max);
}

void test_stepper_position_accepts_position_only() {
  TEST_ASSERT_TRUE(stepAccepts(3, CommandType::POSITION));
  TEST_ASSERT_TRUE(stepAccepts(3, CommandType::OFF));
  TEST_ASSERT_FALSE(stepAccepts(3, CommandType::PULSE));
  TEST_ASSERT_FALSE(stepAccepts(3, CommandType::PWM));
}

void test_stepper_strike_accepts_pulse() {
  TEST_ASSERT_TRUE(stepAccepts(4, CommandType::PULSE));
  TEST_ASSERT_TRUE(stepAccepts(4, CommandType::POSITION));
  TEST_ASSERT_FALSE(stepAccepts(4, CommandType::PWM));
}

void test_stepper_rotate_accepts_pwm_only() {
  TEST_ASSERT_TRUE(stepAccepts(5, CommandType::PWM));
  TEST_ASSERT_FALSE(stepAccepts(5, CommandType::PULSE));
  TEST_ASSERT_FALSE(stepAccepts(5, CommandType::POSITION));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_instrument_passes);
  RUN_TEST(test_note_out_of_range_rejected);
  RUN_TEST(test_channel_out_of_range_rejected);
  RUN_TEST(test_unknown_actuator_rejected);
  RUN_TEST(test_incompatible_command_rejected);
  RUN_TEST(test_cc_binding_command_must_be_position_or_pwm);
  RUN_TEST(test_range_normalization);
  RUN_TEST(test_stepper_position_accepts_position_only);
  RUN_TEST(test_stepper_strike_accepts_pulse);
  RUN_TEST(test_stepper_rotate_accepts_pwm_only);
  return UNITY_END();
}
