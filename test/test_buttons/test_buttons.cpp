#include <unity.h>

#include "../../src/buttons.h"

using namespace desknest;

void setUp(void) {}
void tearDown(void) {}

void test_b_short_emits_once_on_release_and_rearms() {
    ButtonPressTracker tracker;
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_press(tracker, true, 100, 1000, BUTTON_PREV, BUTTON_BACK));
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_press(tracker, true, 500, 1000, BUTTON_PREV, BUTTON_BACK));
    TEST_ASSERT_EQUAL(BUTTON_PREV,
        dn_button_tick_press(tracker, false, 600, 1000, BUTTON_PREV, BUTTON_BACK));
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_press(tracker, false, 700, 1000, BUTTON_PREV, BUTTON_BACK));

    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_press(tracker, true, 800, 1000, BUTTON_PREV, BUTTON_BACK));
    TEST_ASSERT_EQUAL(BUTTON_PREV,
        dn_button_tick_press(tracker, false, 900, 1000, BUTTON_PREV, BUTTON_BACK));
}

void test_b_long_emits_back_once_and_does_not_emit_short_on_release() {
    ButtonPressTracker tracker;
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_press(tracker, true, 100, 1000, BUTTON_PREV, BUTTON_BACK));
    TEST_ASSERT_EQUAL(BUTTON_BACK,
        dn_button_tick_press(tracker, true, 1100, 1000, BUTTON_PREV, BUTTON_BACK));
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_press(tracker, true, 1500, 1000, BUTTON_PREV, BUTTON_BACK));
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_press(tracker, false, 1600, 1000, BUTTON_PREV, BUTTON_BACK));
}

void test_ab_short_does_not_leave_ghost_single_release() {
    ButtonPressTracker a;
    ButtonPressTracker b;
    ButtonPressTracker combo;
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_inputs(a, b, combo, true, true, 100));
    TEST_ASSERT_EQUAL(BUTTON_SELECT,
        dn_button_tick_inputs(a, b, combo, false, false, 200));
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_inputs(a, b, combo, false, false, 300));
}

void test_ab_split_release_suppresses_held_member_ghost() {
    ButtonPressTracker a;
    ButtonPressTracker b;
    ButtonPressTracker combo;
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_inputs(a, b, combo, true, true, 100));
    TEST_ASSERT_EQUAL(BUTTON_SELECT,
        dn_button_tick_inputs(a, b, combo, false, true, 200));
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_inputs(a, b, combo, false, false, 300));
}

void test_ab_long_suppresses_singles_then_b_rearms() {
    ButtonPressTracker a;
    ButtonPressTracker b;
    ButtonPressTracker combo;
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_inputs(a, b, combo, true, true, 100));
    TEST_ASSERT_EQUAL(BUTTON_FACTORY,
        dn_button_tick_inputs(a, b, combo, true, true, 3100));
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_inputs(a, b, combo, false, false, 3200));
    TEST_ASSERT_EQUAL(BUTTON_NONE,
        dn_button_tick_inputs(a, b, combo, false, true, 3300));
    TEST_ASSERT_EQUAL(BUTTON_PREV,
        dn_button_tick_inputs(a, b, combo, false, false, 3400));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_b_short_emits_once_on_release_and_rearms);
    RUN_TEST(test_b_long_emits_back_once_and_does_not_emit_short_on_release);
    RUN_TEST(test_ab_short_does_not_leave_ghost_single_release);
    RUN_TEST(test_ab_split_release_suppresses_held_member_ghost);
    RUN_TEST(test_ab_long_suppresses_singles_then_b_rearms);
    return UNITY_END();
}
