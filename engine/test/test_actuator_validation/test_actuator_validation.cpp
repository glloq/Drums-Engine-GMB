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

// review #6: unknown type/bus must be rejected, not fall through to return true.
void test_invalid_type_rejected() {
  ActuatorConfig c = baseSolenoid();
  c.type = (ActuatorType)200;
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
}

void test_invalid_bus_rejected() {
  ActuatorConfig c = baseSolenoid();
  c.bus = (HardwareBus)200;
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
}

void test_endstop_out_of_range_rejected() {
  ActuatorConfig c = baseSolenoid();
  c.endStopPin1 = 60;                 // not a GPIO, not 255
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
  c.endStopPin1 = 0xFF;               // disabled -> OK again
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
}

void test_mcp_pin_out_of_range_rejected() {
  ActuatorConfig c = baseSolenoid();  // MCP23017 bus
  c.hwPin = 20;
  const char* err = nullptr;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
}

// review #13: ESP32 GPIO capability helpers.
void test_gpio_capability_table() {
  TEST_ASSERT_TRUE(esp32GpioCanOutput(18));   // normal I/O
  TEST_ASSERT_FALSE(esp32GpioCanOutput(34));  // input-only
  TEST_ASSERT_FALSE(esp32GpioCanOutput(6));   // flash
  TEST_ASSERT_FALSE(esp32GpioCanOutput(20));  // not bonded out
  TEST_ASSERT_TRUE(esp32GpioCanInput(34));    // input-only is input-capable
  TEST_ASSERT_FALSE(esp32GpioCanInput(9));    // flash
}

// A direct-GPIO solenoid on an input-only / flash pin must be rejected.
void test_direct_gpio_output_on_bad_pin_rejected() {
  ActuatorConfig c = baseSolenoid();
  c.bus = HardwareBus::GPIO_DIRECT;
  const char* err = nullptr;
  c.hwPin = 34;  // input-only
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
  c.hwPin = 6;   // flash
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
  c.hwPin = 18;  // good
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
}

// End stop on a flash pin is rejected; on an input-only pin it is allowed.
void test_endstop_pin_capability() {
  ActuatorConfig c = baseSolenoid();
  c.bus = HardwareBus::GPIO_DIRECT; c.hwPin = 18;
  const char* err = nullptr;
  c.endStopPin1 = 7;   // flash -> reject
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
  c.endStopPin1 = 35;  // input-only -> ok for a switch
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
}

// ----------------------------------------------------------------------------
// Pins reserved by the engine itself
// ----------------------------------------------------------------------------

// The I2C bus, the status LED and the BOOT strapping pin can never be handed to
// an actuator: taking GPIO 21/22 would drop every MCP23017/PCA9685 actuator.
void test_exclusively_reserved_pins_rejected() {
  const char* err = nullptr;
  const uint8_t exclusive[] = { I2C_SDA, I2C_SCL, STATUS_LED_PIN };

  for (uint8_t i = 0; i < sizeof(exclusive) / sizeof(exclusive[0]); i++) {
    ActuatorConfig c = baseSolenoid();
    c.bus = HardwareBus::GPIO_DIRECT;
    c.hwPin = exclusive[i];
    TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
    TEST_ASSERT_NOT_NULL(err);
  }

  // ... and not just as the output pin: an end stop on the I2C bus is just as
  // destructive.
  ActuatorConfig c = baseSolenoid();
  c.bus = HardwareBus::GPIO_DIRECT;
  c.hwPin = 18;
  c.endStopPin1 = I2C_SCL;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
}

// Optional peripherals (I2S mic, default LED strip pins) stay usable: they are
// only wired when the user enables that feature.
void test_optional_reserved_pins_allowed() {
  ActuatorConfig c = baseSolenoid();
  c.bus = HardwareBus::GPIO_DIRECT;
  c.hwPin = MIC_I2S_SD;          // 33 — free unless a microphone is fitted
  const char* err = nullptr;
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));

  const PinReservation* r = esp32PinReservation(MIC_I2S_SD);
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_FALSE(r->exclusive);
}

// The table must stay consistent with the pins the firmware actually drives.
void test_reservation_table_lookup() {
  uint8_t count = 0;
  const PinReservation* all = esp32PinReservations(count);
  TEST_ASSERT_NOT_NULL(all);
  TEST_ASSERT_TRUE(count > 0);

  const PinReservation* sda = esp32PinReservation(I2C_SDA);
  TEST_ASSERT_NOT_NULL(sda);
  TEST_ASSERT_TRUE(sda->exclusive);

  TEST_ASSERT_NULL(esp32PinReservation(18));  // ordinary free GPIO
}

// ----------------------------------------------------------------------------
// Hardware resource conflicts
// ----------------------------------------------------------------------------

void test_conflict_same_gpio() {
  ActuatorConfig a = baseSolenoid();
  a.bus = HardwareBus::GPIO_DIRECT; a.hwPin = 18;
  ActuatorConfig b = a;
  b.hwPin = 19;
  TEST_ASSERT_FALSE(actuatorConfigsConflict(a, b));
  b.hwPin = 18;                       // the case the UPDATE path used to accept
  TEST_ASSERT_TRUE(actuatorConfigsConflict(a, b));
}

void test_conflict_endstop_vs_output() {
  ActuatorConfig a = baseSolenoid();
  a.bus = HardwareBus::GPIO_DIRECT; a.hwPin = 18;
  ActuatorConfig b = baseSolenoid();
  b.bus = HardwareBus::GPIO_DIRECT; b.hwPin = 19; b.endStopPin1 = 18;
  TEST_ASSERT_TRUE(actuatorConfigsConflict(a, b));
}

void test_conflict_expander_channel() {
  ActuatorConfig a = baseSolenoid();   // MCP23017 bus by default
  a.hwAddress = 0x20; a.hwPin = 3;
  ActuatorConfig b = a;
  b.hwPin = 4;
  TEST_ASSERT_FALSE(actuatorConfigsConflict(a, b));
  b.hwPin = 3;
  TEST_ASSERT_TRUE(actuatorConfigsConflict(a, b));
  b.hwAddress = 0x21;                  // different module, same pin -> fine
  TEST_ASSERT_FALSE(actuatorConfigsConflict(a, b));
}

// An expander actuator and a direct-GPIO one never share a resource, even when
// the raw numbers happen to match (MCP pin 3 is not GPIO 3).
void test_no_conflict_across_buses() {
  ActuatorConfig a = baseSolenoid();
  a.hwAddress = 0x20; a.hwPin = 18;
  ActuatorConfig b = baseSolenoid();
  b.bus = HardwareBus::GPIO_DIRECT; b.hwPin = 18;
  TEST_ASSERT_FALSE(actuatorConfigsConflict(a, b));
}

// ----------------------------------------------------------------------------
// PWM capability — the hazardous cases
// ----------------------------------------------------------------------------

// SOLENOID_HOLD drops the coil to a reduced holding current. On a digital-only
// bus both levels came out at 100 % duty, leaving the coil at full current.
void test_solenoid_hold_requires_real_pwm() {
  const char* err = nullptr;
  ActuatorConfig c = baseSolenoid();
  c.behavior = ActuatorBehavior::SOLENOID_HOLD;
  c.paramMin = 255;   // strike
  c.paramMax = 60;    // hold
  c.paramDefault = 20;

  c.bus = HardwareBus::MCP23017;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
  c.bus = HardwareBus::GPIO_DIRECT; c.hwPin = 18;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));

  c.bus = HardwareBus::LEDC_PWM; c.hwPin = 18;
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
}

// A "hold" that is not lower than the strike is a permanently energised coil.
void test_solenoid_hold_must_reduce_current() {
  const char* err = nullptr;
  ActuatorConfig c = baseSolenoid();
  c.behavior = ActuatorBehavior::SOLENOID_HOLD;
  c.bus = HardwareBus::LEDC_PWM; c.hwPin = 18;
  c.paramDefault = 20;

  c.paramMin = 200; c.paramMax = 200;          // equal -> reject
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
  c.paramMin = 100; c.paramMax = 250;          // hold above strike -> reject
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
  c.paramMin = 255; c.paramMax = 60;           // proper strike/hold
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
}

// STRIKE and MUTE are bang-bang, so they remain valid on the digital buses.
void test_solenoid_strike_still_allowed_on_digital_buses() {
  const char* err = nullptr;
  ActuatorConfig c = baseSolenoid();          // MCP23017 / SOLENOID_STRIKE
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
  c.bus = HardwareBus::GPIO_DIRECT; c.hwPin = 18;
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
}

static ActuatorConfig baseMotor() {
  ActuatorConfig c;
  c.type = ActuatorType::PWM_MOTOR;
  c.behavior = ActuatorBehavior::MOTOR_SPEED;
  c.bus = HardwareBus::LEDC_PWM;
  c.hwPin = 18;
  c.paramMin = 30; c.paramMax = 100; c.paramDefault = 50;
  return c;
}

// PCA9685 is clocked at 50 Hz for servos — unusable as a motor drive.
void test_pwm_motor_requires_ledc() {
  const char* err = nullptr;
  ActuatorConfig c = baseMotor();
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
  c.bus = HardwareBus::PCA9685; c.hwPin = 3;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
}

// paramMin/paramMax are percentages; anything above 100 is a unit error.
void test_motor_percent_range_enforced() {
  const char* err = nullptr;
  ActuatorConfig c = baseMotor();
  c.paramMin = 0; c.paramMax = 100; c.paramDefault = 50;
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
  c.paramMax = 255;                      // raw duty mistaken for a percentage
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
}

// Percent -> normalised duty, and the 8-bit helper used by SOLENOID_HOLD.
void test_duty_conversion_helpers() {
  TEST_ASSERT_EQUAL_UINT16(0, pwmDutyFromPercent(0));
  TEST_ASSERT_EQUAL_UINT16(PWM_DUTY_MAX, pwmDutyFromPercent(100));
  TEST_ASSERT_EQUAL_UINT16(PWM_DUTY_MAX, pwmDutyFromPercent(150));   // clamped
  TEST_ASSERT_UINT16_WITHIN(20, PWM_DUTY_MAX / 2, pwmDutyFromPercent(50));

  TEST_ASSERT_EQUAL_UINT16(0, pwmDutyFrom8Bit(0));
  TEST_ASSERT_EQUAL_UINT16(PWM_DUTY_MAX, pwmDutyFrom8Bit(255));
  TEST_ASSERT_UINT16_WITHIN(20, PWM_DUTY_MAX / 2, pwmDutyFrom8Bit(128));
}

// Servo angles drive a fixed 150..600 pulse window; >180 deg runs the horn
// past its mechanical stop.
void test_servo_angle_range_enforced() {
  const char* err = nullptr;
  ActuatorConfig c;
  c.type = ActuatorType::SERVO;
  c.behavior = ActuatorBehavior::SERVO_POSITION;
  c.bus = HardwareBus::PCA9685;
  c.hwAddress = 0x40; c.hwPin = 0;
  c.paramMin = 0; c.paramMax = 180; c.paramDefault = 90;
  TEST_ASSERT_TRUE(validateActuatorConfig(c, err));
  c.paramMax = 200;
  TEST_ASSERT_FALSE(validateActuatorConfig(c, err));
}

// Cross-subsystem pin ownership: LED strips and the mic need this too.
void test_actuator_config_uses_gpio() {
  ActuatorConfig c = baseSolenoid();
  c.bus = HardwareBus::GPIO_DIRECT;
  c.hwPin = 18;
  c.endStopPin1 = 19;
  TEST_ASSERT_TRUE(actuatorConfigUsesGpio(c, 18));
  TEST_ASSERT_TRUE(actuatorConfigUsesGpio(c, 19));
  TEST_ASSERT_FALSE(actuatorConfigUsesGpio(c, 21));
  TEST_ASSERT_FALSE(actuatorConfigUsesGpio(c, 0xFF));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_solenoid_hold_requires_real_pwm);
  RUN_TEST(test_solenoid_hold_must_reduce_current);
  RUN_TEST(test_solenoid_strike_still_allowed_on_digital_buses);
  RUN_TEST(test_pwm_motor_requires_ledc);
  RUN_TEST(test_motor_percent_range_enforced);
  RUN_TEST(test_duty_conversion_helpers);
  RUN_TEST(test_servo_angle_range_enforced);
  RUN_TEST(test_actuator_config_uses_gpio);
  RUN_TEST(test_exclusively_reserved_pins_rejected);
  RUN_TEST(test_optional_reserved_pins_allowed);
  RUN_TEST(test_reservation_table_lookup);
  RUN_TEST(test_conflict_same_gpio);
  RUN_TEST(test_conflict_endstop_vs_output);
  RUN_TEST(test_conflict_expander_channel);
  RUN_TEST(test_no_conflict_across_buses);
  RUN_TEST(test_gpio_capability_table);
  RUN_TEST(test_direct_gpio_output_on_bad_pin_rejected);
  RUN_TEST(test_endstop_pin_capability);
  RUN_TEST(test_valid_solenoid_passes);
  RUN_TEST(test_paramMin_gt_paramMax_rejected);
  RUN_TEST(test_default_outside_range_rejected);
  RUN_TEST(test_servo_requires_pca_bus);
  RUN_TEST(test_servo_channel_out_of_range_rejected);
  RUN_TEST(test_direct_gpio_pin_out_of_range_rejected);
  RUN_TEST(test_mismatched_behavior_rejected);
  RUN_TEST(test_invalid_type_rejected);
  RUN_TEST(test_invalid_bus_rejected);
  RUN_TEST(test_endstop_out_of_range_rejected);
  RUN_TEST(test_mcp_pin_out_of_range_rejected);
  return UNITY_END();
}
