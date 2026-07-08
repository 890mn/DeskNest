#include <unity.h>

#include "../../src/environment_module.h"

using namespace desknest;

void test_environment_module_scores_comfortable_room_as_good() {
    EnvironmentInput in = {};
    in.valid = true;
    in.temperatureC = 24.0f;
    in.humidityPct = 50.0f;
    in.lux = 300;

    EnvironmentStatus status = dn_evaluate_environment(in);

    TEST_ASSERT_TRUE(status.valid);
    TEST_ASSERT_EQUAL_UINT8(100, status.score);
    TEST_ASSERT_EQUAL_STRING("良好", status.gradeText);
    TEST_ASSERT_EQUAL_STRING("OK", status.temperatureGrade);
    TEST_ASSERT_EQUAL_STRING("OK", status.humidityGrade);
    TEST_ASSERT_EQUAL_STRING("OK", status.lightGrade);
    TEST_ASSERT_EQUAL_STRING("保持专注", status.adviceText);
}

void test_environment_module_invalid_reading_scores_zero() {
    EnvironmentInput in = {};
    in.valid = false;

    EnvironmentStatus status = dn_evaluate_environment(in);

    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL_UINT8(0, status.score);
    TEST_ASSERT_EQUAL_STRING("欠佳", status.gradeText);
    TEST_ASSERT_EQUAL_STRING("--", status.temperatureGrade);
    TEST_ASSERT_EQUAL_STRING("--", status.humidityGrade);
    TEST_ASSERT_EQUAL_STRING("--", status.lightGrade);
    TEST_ASSERT_EQUAL_STRING("等待传感器", status.adviceText);
}

void test_environment_module_prioritizes_actionable_advice() {
    EnvironmentInput in = {};
    in.valid = true;
    in.temperatureC = 31.0f;
    in.humidityPct = 25.0f;
    in.lux = 80;

    EnvironmentStatus status = dn_evaluate_environment(in);

    TEST_ASSERT_EQUAL_STRING("偏热", status.temperatureGrade);
    TEST_ASSERT_EQUAL_STRING("干燥", status.humidityGrade);
    TEST_ASSERT_EQUAL_STRING("偏暗", status.lightGrade);
    TEST_ASSERT_EQUAL_STRING("建议通风", status.adviceText);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_environment_module_scores_comfortable_room_as_good);
    RUN_TEST(test_environment_module_invalid_reading_scores_zero);
    RUN_TEST(test_environment_module_prioritizes_actionable_advice);
    return UNITY_END();
}

