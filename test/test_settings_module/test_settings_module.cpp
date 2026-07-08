#include <unity.h>

#include "../../src/settings_module.h"

using namespace desknest;

void test_settings_module_returns_default_rows() {
    SettingsStatus status = dn_settings_default_status();

    TEST_ASSERT_EQUAL_UINT8(5, status.rowCount);
    TEST_ASSERT_EQUAL_STRING("Power", status.rows[0].label);
    TEST_ASSERT_EQUAL_STRING("Balanced", status.rows[0].value);
    TEST_ASSERT_EQUAL_STRING("Sync", status.rows[1].label);
    TEST_ASSERT_EQUAL_STRING("Battery", status.rows[1].value);
    TEST_ASSERT_EQUAL_STRING("Density", status.rows[2].label);
    TEST_ASSERT_EQUAL_STRING("Normal", status.rows[2].value);
    TEST_ASSERT_EQUAL_STRING("Rotate", status.rows[3].label);
    TEST_ASSERT_EQUAL_STRING("Auto", status.rows[3].value);
    TEST_ASSERT_EQUAL_STRING("Theme", status.rows[4].label);
    TEST_ASSERT_EQUAL_STRING("Dark", status.rows[4].value);
    TEST_ASSERT_EQUAL_STRING("[A+B] Factory", status.dangerHint);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_settings_module_returns_default_rows);
    return UNITY_END();
}

