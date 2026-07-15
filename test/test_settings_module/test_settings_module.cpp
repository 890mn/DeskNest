#include <unity.h>

#include "../../src/device_settings.cpp"
#include "../../src/settings_module.h"

using namespace desknest;

void test_settings_module_returns_default_rows() {
    const DeviceSettings settings = dn_settings_defaults();
    SettingsStatus status = dn_settings_status(settings, 0);

    TEST_ASSERT_EQUAL_UINT8(3, status.rowCount);
    TEST_ASSERT_EQUAL_UINT8(0, status.selectedIndex);
    TEST_ASSERT_TRUE(status.rows[0].selectable);
    TEST_ASSERT_TRUE(status.rows[1].selectable);
    TEST_ASSERT_EQUAL_STRING("首页焦点", status.rows[0].label);
    TEST_ASSERT_EQUAL_STRING("80%", status.rows[1].value);
    TEST_ASSERT_EQUAL_STRING("标准", status.rows[2].value);
    TEST_ASSERT_EQUAL_UINT8(4, dn_settings_option_count(0));
    TEST_ASSERT_EQUAL_UINT8(4, dn_settings_option_count(1));
    TEST_ASSERT_EQUAL_UINT8(3, dn_settings_option_count(2));
}

void test_settings_schema_rejects_invalid_and_loads_defaults() {
    dn_settings_test_reset_store();
    DeviceSettings invalid = dn_settings_defaults();
    invalid.schemaVersion = 9;
    dn_settings_test_seed_store(invalid);

    DeviceSettings loaded = {};
    TEST_ASSERT_FALSE(dn_settings_load(&loaded));
    TEST_ASSERT_TRUE(dn_settings_valid(loaded));
    TEST_ASSERT_EQUAL_UINT8(80, dn_settings_ai_threshold(loaded));
}

void test_ai_alert_threshold_is_inclusive() {
    DeviceSettings settings = dn_settings_defaults();
    settings.aiAlertIndex = 2;
    TEST_ASSERT_FALSE(dn_settings_ai_alert_active(84, settings));
    TEST_ASSERT_TRUE(dn_settings_ai_alert_active(85, settings));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_settings_module_returns_default_rows);
    RUN_TEST(test_settings_schema_rejects_invalid_and_loads_defaults);
    RUN_TEST(test_ai_alert_threshold_is_inclusive);
    return UNITY_END();
}
