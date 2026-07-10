#include <unity.h>

#include "../../src/settings_module.h"

using namespace desknest;

void test_settings_module_returns_default_rows() {
    const uint8_t values[4] = {0, 0, 0, 0};
    SettingsStatus status = dn_settings_status(values, 0);

    TEST_ASSERT_EQUAL_UINT8(4, status.rowCount);
    TEST_ASSERT_EQUAL_UINT8(0, status.selectedIndex);
    TEST_ASSERT_TRUE(status.rows[0].selectable);
    TEST_ASSERT_TRUE(status.rows[1].selectable);
    TEST_ASSERT_EQUAL_UINT8(4, dn_settings_option_count(0));
    TEST_ASSERT_EQUAL_UINT8(2, dn_settings_option_count(1));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_settings_module_returns_default_rows);
    return UNITY_END();
}
