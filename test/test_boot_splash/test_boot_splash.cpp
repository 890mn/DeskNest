#include <unity.h>

#include "../../src/boot_splash.cpp"

using namespace desknest;

void setUp(void) {}
void tearDown(void) {}

void test_boot_splash_stays_visible_before_fade() {
    dn_boot_splash_begin(1000);
    dn_boot_splash_update(3900, true, true, false, true);

    const BootSplashStatus status = dn_boot_splash_status();
    TEST_ASSERT_TRUE(status.active);
    TEST_ASSERT_TRUE(status.k10Ready);
    TEST_ASSERT_TRUE(status.wifiReady);
    TEST_ASSERT_FALSE(status.timeReady);
    TEST_ASSERT_TRUE(status.aiReady);
    TEST_ASSERT_FALSE(status.ready);
    TEST_ASSERT_EQUAL_UINT8(0, status.fadePct);
}

void test_boot_splash_waits_for_all_tasks_even_after_min_hold() {
    dn_boot_splash_begin(1000);
    dn_boot_splash_update(5000, true, true, true, false);

    const BootSplashStatus status = dn_boot_splash_status();
    TEST_ASSERT_TRUE(status.active);
    TEST_ASSERT_FALSE(status.ready);
    TEST_ASSERT_EQUAL_UINT8(0, status.fadePct);
}

void test_boot_splash_reports_visual_progress_before_ready() {
    dn_boot_splash_begin(1000);
    dn_boot_splash_update(5000, true, false, false, false);

    const BootSplashStatus status = dn_boot_splash_status();
    TEST_ASSERT_TRUE(status.active);
    TEST_ASSERT_GREATER_THAN_UINT8(0, status.progressPct);
    TEST_ASSERT_LESS_THAN_UINT8(100, status.progressPct);
}

void test_boot_splash_finishes_immediately_after_all_tasks_ready() {
    dn_boot_splash_begin(1000);
    dn_boot_splash_update(4000, true, true, true, true);

    const BootSplashStatus status = dn_boot_splash_status();
    TEST_ASSERT_FALSE(status.active);
    TEST_ASSERT_TRUE(status.ready);
    TEST_ASSERT_EQUAL_UINT8(100, status.fadePct);
}

void test_boot_splash_finishes_after_total_window() {
    dn_boot_splash_begin(1000);
    dn_boot_splash_update(4000, true, true, true, true);
    dn_boot_splash_update(4500, true, true, true, true);

    const BootSplashStatus status = dn_boot_splash_status();
    TEST_ASSERT_FALSE(status.active);
    TEST_ASSERT_TRUE(status.ready);
    TEST_ASSERT_EQUAL_UINT8(100, status.fadePct);
}

void test_boot_splash_hides_and_marks_failure() {
    dn_boot_splash_begin(1000);
    dn_boot_splash_update(2500, true, false, false, false, true, BOOT_FAIL_WIFI);

    const BootSplashStatus status = dn_boot_splash_status();
    TEST_ASSERT_FALSE(status.active);
    TEST_ASSERT_TRUE(status.failed);
    TEST_ASSERT_EQUAL(BOOT_FAIL_WIFI, status.failureReason);
    TEST_ASSERT_EQUAL_UINT8(100, status.fadePct);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_boot_splash_stays_visible_before_fade);
    RUN_TEST(test_boot_splash_waits_for_all_tasks_even_after_min_hold);
    RUN_TEST(test_boot_splash_reports_visual_progress_before_ready);
    RUN_TEST(test_boot_splash_finishes_immediately_after_all_tasks_ready);
    RUN_TEST(test_boot_splash_finishes_after_total_window);
    RUN_TEST(test_boot_splash_hides_and_marks_failure);
    return UNITY_END();
}
