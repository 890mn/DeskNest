#include <unity.h>

#include "../../src/sensors.h"

using namespace desknest;

void test_aht20_temperature_compensation_defaults_to_minus_six_degrees() {
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f,
        24.5f,
        dn_apply_aht20_temperature_compensation(30.5f));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_aht20_temperature_compensation_defaults_to_minus_six_degrees);
    return UNITY_END();
}
