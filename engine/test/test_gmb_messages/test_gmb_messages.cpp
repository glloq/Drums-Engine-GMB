#include <unity.h>

#include <string>

#include "gmb_test_fixture.h"
#include "../../src/gmb/gmb_capabilities.cpp"
#include "../../src/gmb/gmb_descriptor.cpp"

static GmbFixture fx;
static CapabilitySnapshot snap;

void setUp() {
  fx.reset();
  snap.clear();
}

void tearDown() {}

static GmbBuildInputs inputs() {
  GmbBuildInputs in;
  in.lookup = &fx.lookup;
  in.actuatorLookup = GmbFixture::resolve;
  in.actuatorCtx = &fx;
  in.channelMask = 0xFFFF;
  in.power.maxConcurrent = 8;
  return in;
}

static std::string serialize(const CapabilitySnapshot& s) {
  char out[4096];
  const GmbDeviceInfo device{"Test drums", "Drums-Engine-GMB"};
  const GmbSerializeResult res = gmbSerializeDescriptor(out, sizeof(out) - 1, s, 1, device);
  TEST_ASSERT_TRUE(res.length > 0);
  return std::string(out, res.length);
}

void test_gm_kit_publishes_note_and_cc_message_support() {
  fx.buildGmKit(10);
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);

  const std::string d = serialize(snap);
  TEST_ASSERT_TRUE(d.find("\"messages\":{") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"note_on\":true") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"note_off\":true") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"control_change\":true") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"program_change\":false") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"pitch_bend\":false") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"channel_aftertouch\":false") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"poly_aftertouch\":false") != std::string::npos);

  // Realtime remains tri-state unknown until the runtime provides positive or
  // negative semantic evidence; do not invent false capabilities here.
  TEST_ASSERT_TRUE(d.find("\"clock\"") == std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"start\"") == std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"continue\"") == std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"stop\"") == std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"system_reset\"") == std::string::npos);
}

void test_virtual_routes_publish_pitch_bend_and_channel_aftertouch() {
  const uint8_t drum =
      fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  const uint8_t tension =
      fx.addActuator(ActuatorType::SERVO, ActuatorBehavior::PITCH_BEND);
  GmbFixture::addNoteOn(fx.addPipeline(10, 47), GmbFixture::strike(drum, 10, 30));
  fx.addCcRoute(VIRTUAL_CC_PITCH_BEND, tension, 10);
  fx.addCcRoute(VIRTUAL_CC_AFTERTOUCH, tension, 10);
  fx.finish();

  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(1, snap.instrumentCount);
  TEST_ASSERT_TRUE(snap.instruments[0].ccs.empty());

  const std::string d = serialize(snap);
  TEST_ASSERT_TRUE(d.find("\"control_change\":false") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"pitch_bend\":true") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"channel_aftertouch\":true") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"poly_aftertouch\":false") != std::string::npos);
}

void test_unconfigured_placeholder_does_not_claim_message_capabilities() {
  GmbBuildInputs in = inputs();
  gmbBuildCapabilities(in, snap);
  TEST_ASSERT_EQUAL_UINT8(0, snap.instrumentCount);

  const std::string d = serialize(snap);
  TEST_ASSERT_TRUE(d.find("\"configured\":false") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"messages\"") == std::string::npos);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_gm_kit_publishes_note_and_cc_message_support);
  RUN_TEST(test_virtual_routes_publish_pitch_bend_and_channel_aftertouch);
  RUN_TEST(test_unconfigured_placeholder_does_not_claim_message_capabilities);
  return UNITY_END();
}
