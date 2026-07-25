// ============================================================================
// Native unit tests for the shared actuator config validator (Lot 3 #15)
// ============================================================================
#include <unity.h>
#include "../../src/actuator/actuator_validation.cpp"

static ActuatorConfig baseSolenoid() {
  ActuatorConfig c;               // defaults: SOLENOID / SOLENOID_STRIKE / MCP23017
  c.paramMin = 8; c.paramMax = 30; c.paramDefault = 15;
  return c;
}

void setUp() {}
void tearDown() {}

void test_valid_solenoid_passes() {
  ActuatorConfig c = baseSolenoid();
  const char* err = nullptr;
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
  TEST_ASSERT_NULL(err);
}

void test_paramMin_gt_paramMax_rejected() {
  ActuatorConfig c = baseSolenoid();
  c.paramMin = 40; c.paramMax = 10;   // invalid for non-HOLD behavior
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_default_outside_range_rejected() {
  ActuatorConfig c = baseSolenoid();
  c.paramDefault = 99;                 // outside [8,30]
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
}

void test_servo_requires_pca_bus() {
  ActuatorConfig c;
  c.type = ActuatorType::SERVO;
  c.behavior = ActuatorBehavior::SERVO_POSITION;
  c.bus = HardwareBus::MCP23017;       // wrong bus for servo
  c.paramMin = 0; c.paramMax = 180; c.paramDefault = 90;
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));

  c.bus = HardwareBus::PCA9685;        // correct
  c.hwPin = 3;
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
}

void test_servo_channel_out_of_range_rejected() {
  ActuatorConfig c;
  c.type = ActuatorType::SERVO;
  c.behavior = ActuatorBehavior::SERVO_POSITION;
  c.bus = HardwareBus::PCA9685;
  c.hwPin = 20;                        // PCA9685 has 16 channels (0..15)
  c.paramMin = 0; c.paramMax = 180; c.paramDefault = 90;
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
}

void test_direct_gpio_pin_out_of_range_rejected() {
  ActuatorConfig c = baseSolenoid();
  c.bus = HardwareBus::GPIO_DIRECT;
  c.hwPin = 55;                        // not a valid ESP32 GPIO
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
}

void test_mismatched_behavior_rejected() {
  ActuatorConfig c = baseSolenoid();
  c.behavior = ActuatorBehavior::SERVO_POSITION;  // servo behavior on a solenoid
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_solenoid_passes);
  RUN_TEST(test_paramMin_gt_paramMax_rejected);
  RUN_TEST(test_default_outside_range_rejected);
  RUN_TEST(test_servo_requires_pca_bus);
  RUN_TEST(test_servo_channel_out_of_range_rejected);
  RUN_TEST(test_direct_gpio_pin_out_of_range_rejected);
  RUN_TEST(test_mismatched_behavior_rejected);
  return UNITY_END();
}
