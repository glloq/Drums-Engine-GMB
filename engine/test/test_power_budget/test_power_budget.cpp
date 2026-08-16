// ============================================================================
// Native unit tests for the power-budget arbitration (core/power_budget.h)
// ============================================================================
// The overload protection used to be a flat "at most 8 active actuators",
// which treats a 20 mA hi-hat servo and a 2 A bass-drum solenoid as equal. The
// rules below replace it with an arbitration on declared current — while
// keeping the count ceiling, and keeping an undeclared-current configuration
// behaving exactly as it did before.
#include <unity.h>
#include "../../src/core/power_budget.h"

static PowerBudgetConfig noLimits() {
  PowerBudgetConfig b;
  b.maxPeakMa = 0;
  b.maxContinuousMa = 0;
  b.maxConcurrent = 0;
  return b;
}

static const uint8_t P_LOW    = (uint8_t)ActuatorPriority::PRIO_LOW;
static const uint8_t P_NORMAL = (uint8_t)ActuatorPriority::PRIO_NORMAL;
static const uint8_t P_HIGH   = (uint8_t)ActuatorPriority::PRIO_HIGH;

void setUp() {}
void tearDown() {}

// --- Retro-compatibilite --------------------------------------------------

void test_defaults_reproduce_the_historical_count_limit() {
  PowerBudgetConfig b;   // defauts compiles
  TEST_ASSERT_EQUAL_UINT16(MAX_CONCURRENT_ACTIVE, b.maxConcurrent);
  TEST_ASSERT_EQUAL_UINT16(0, b.maxPeakMa);
  TEST_ASSERT_EQUAL_UINT16(0, b.maxContinuousMa);

  // 7 actifs, courant non declare partout : admis, comme avant.
  TEST_ASSERT_EQUAL(PowerVerdict::ADMIT,
                    powerBudgetAdmit(b, MAX_CONCURRENT_ACTIVE - 1, 0, 0, P_NORMAL));
  // Le 9e est refuse, comme avant.
  TEST_ASSERT_EQUAL(PowerVerdict::REFUSED_COUNT,
                    powerBudgetAdmit(b, MAX_CONCURRENT_ACTIVE, 0, 0, P_NORMAL));
}

void test_undeclared_current_never_refused_on_current_grounds() {
  PowerBudgetConfig b = noLimits();
  b.maxPeakMa = 1000;
  b.maxContinuousMa = 500;

  // Deja 5 A tires par des actionneurs declares, mais celui-ci ne declare rien:
  // il reste invisible pour le budget plutot que de deviner une valeur.
  TEST_ASSERT_EQUAL(PowerVerdict::ADMIT,
                    powerBudgetAdmit(b, 3, 5000, 0, P_LOW));
}

// --- Plafond en nombre ----------------------------------------------------

void test_count_ceiling_applies_before_current() {
  PowerBudgetConfig b = noLimits();
  b.maxConcurrent = 4;
  TEST_ASSERT_EQUAL(PowerVerdict::ADMIT, powerBudgetAdmit(b, 3, 0, 100, P_HIGH));
  TEST_ASSERT_EQUAL(PowerVerdict::REFUSED_COUNT, powerBudgetAdmit(b, 4, 0, 100, P_HIGH));
}

void test_count_ceiling_zero_disables_it() {
  PowerBudgetConfig b = noLimits();
  TEST_ASSERT_EQUAL(PowerVerdict::ADMIT, powerBudgetAdmit(b, 200, 0, 100, P_LOW));
}

// --- Plafond de crete -----------------------------------------------------

void test_peak_ceiling_is_hard_for_every_priority() {
  PowerBudgetConfig b = noLimits();
  b.maxPeakMa = 10000;              // alimentation 10 A

  // 9 A deja tires, une grosse caisse a 2 A ne passe pas, meme en HIGH.
  TEST_ASSERT_EQUAL(PowerVerdict::REFUSED_PEAK, powerBudgetAdmit(b, 4, 9000, 2000, P_HIGH));
  TEST_ASSERT_EQUAL(PowerVerdict::REFUSED_PEAK, powerBudgetAdmit(b, 4, 9000, 2000, P_NORMAL));
  TEST_ASSERT_EQUAL(PowerVerdict::REFUSED_PEAK, powerBudgetAdmit(b, 4, 9000, 2000, P_LOW));

  // Un servo de 20 mA passe encore.
  TEST_ASSERT_EQUAL(PowerVerdict::ADMIT, powerBudgetAdmit(b, 4, 9000, 20, P_LOW));
}

void test_peak_ceiling_boundary_is_inclusive() {
  PowerBudgetConfig b = noLimits();
  b.maxPeakMa = 1000;
  // Atteindre exactement le plafond est permis; le depasser d'1 mA ne l'est pas.
  TEST_ASSERT_EQUAL(PowerVerdict::ADMIT, powerBudgetAdmit(b, 0, 900, 100, P_NORMAL));
  TEST_ASSERT_EQUAL(PowerVerdict::REFUSED_PEAK, powerBudgetAdmit(b, 0, 900, 101, P_NORMAL));
}

// --- Plafond continu / priorites -----------------------------------------

void test_continuous_ceiling_sheds_ornaments_first() {
  PowerBudgetConfig b = noLimits();
  b.maxPeakMa = 10000;              // 10 A en crete
  b.maxContinuousMa = 4000;         // 4 A en continu

  // Au-dessus de 4 A: un tambourin d'ornement est sacrifie...
  TEST_ASSERT_EQUAL(PowerVerdict::REFUSED_CONTINUOUS,
                    powerBudgetAdmit(b, 3, 3900, 500, P_LOW));
  // ...mais le kick et la caisse claire passent jusqu'au plafond de crete.
  TEST_ASSERT_EQUAL(PowerVerdict::ADMIT, powerBudgetAdmit(b, 3, 3900, 500, P_NORMAL));
  TEST_ASSERT_EQUAL(PowerVerdict::ADMIT, powerBudgetAdmit(b, 3, 3900, 500, P_HIGH));
}

void test_low_priority_admitted_below_continuous_ceiling() {
  PowerBudgetConfig b = noLimits();
  b.maxPeakMa = 10000;
  b.maxContinuousMa = 4000;
  TEST_ASSERT_EQUAL(PowerVerdict::ADMIT, powerBudgetAdmit(b, 1, 1000, 500, P_LOW));
}

void test_continuous_ceiling_alone_still_gates_low() {
  // Plafond continu sans plafond de crete: configuration valide.
  PowerBudgetConfig b = noLimits();
  b.maxContinuousMa = 2000;
  TEST_ASSERT_EQUAL(PowerVerdict::REFUSED_CONTINUOUS,
                    powerBudgetAdmit(b, 0, 1900, 200, P_LOW));
  TEST_ASSERT_EQUAL(PowerVerdict::ADMIT, powerBudgetAdmit(b, 0, 1900, 200, P_NORMAL));
}

// --- Scenario complet -----------------------------------------------------

void test_large_kit_no_longer_capped_at_eight_voices() {
  // Le point du changement : une installation correctement alimentee doit
  // pouvoir frapper plus de 8 notes a la fois si le courant le permet.
  PowerBudgetConfig b = noLimits();
  b.maxPeakMa = 15000;              // 15 A
  b.maxConcurrent = 0;              // plafond en nombre desactive

  // 20 lames de xylophone a 300 mA = 6 A: toutes admises.
  uint32_t active = 0;
  for (uint8_t i = 0; i < 20; i++) {
    TEST_ASSERT_EQUAL(PowerVerdict::ADMIT, powerBudgetAdmit(b, i, active, 300, P_NORMAL));
    active += 300;
  }
  TEST_ASSERT_EQUAL_UINT32(6000, active);

  // La 21e frappe reste admise; c'est bien le courant, pas un compteur, qui
  // finit par trancher.
  TEST_ASSERT_EQUAL(PowerVerdict::ADMIT, powerBudgetAdmit(b, 20, active, 300, P_NORMAL));
  TEST_ASSERT_EQUAL(PowerVerdict::REFUSED_PEAK, powerBudgetAdmit(b, 20, active, 9100, P_HIGH));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_reproduce_the_historical_count_limit);
  RUN_TEST(test_undeclared_current_never_refused_on_current_grounds);
  RUN_TEST(test_count_ceiling_applies_before_current);
  RUN_TEST(test_count_ceiling_zero_disables_it);
  RUN_TEST(test_peak_ceiling_is_hard_for_every_priority);
  RUN_TEST(test_peak_ceiling_boundary_is_inclusive);
  RUN_TEST(test_continuous_ceiling_sheds_ornaments_first);
  RUN_TEST(test_low_priority_admitted_below_continuous_ceiling);
  RUN_TEST(test_continuous_ceiling_alone_still_gates_low);
  RUN_TEST(test_large_kit_no_longer_capped_at_eight_voices);
  return UNITY_END();
}
