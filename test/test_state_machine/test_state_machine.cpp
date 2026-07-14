#include <unity.h>

#include "../../src/gesture.cpp"
#include "../../src/state_machine.cpp"

using namespace desknest;

namespace desknest {
int g_test_what2eat_pick_calls = 0;
bool dn_what2eat_pick() {
    ++g_test_what2eat_pick_calls;
    return true;
}
}

uint32_t g_mock_millis = 0;
SerialClass Serial;

static void resetClock(uint32_t ms = 1000) {
    g_mock_millis = ms;
}

void setUp(void) {
    resetClock();
    g_gesture.begin();
    g_state.begin();
    g_test_what2eat_pick_calls = 0;
}

void tearDown(void) {}

void test_runtime_rotate_to_landscape_is_ignored_for_current_mvp() {
    g_state.updateGesture(GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE, g_mock_millis);

    const StateSnapshot& s = g_state.snapshot();
    TEST_ASSERT_EQUAL(ORIENTATION_PORTRAIT, s.orientation);
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_OVERVIEW, s.page);
}

void test_runtime_orientation_landscape_detection_is_ignored_for_current_mvp() {
    g_state.updateOrientation(ORIENTATION_LANDSCAPE, g_mock_millis);

    const StateSnapshot& s = g_state.snapshot();
    TEST_ASSERT_EQUAL(ORIENTATION_PORTRAIT, s.orientation);
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_OVERVIEW, s.page);
}

void test_face_down_and_face_up_restore_portrait_page() {
    g_state.updateGesture(GESTURE_SHAKE_RIGHT, g_mock_millis);
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_AI_USAGE, g_state.snapshot().page);

    g_mock_millis += 100;
    g_state.updateFace(GESTURE_FACE_DOWN, g_mock_millis);
    TEST_ASSERT_EQUAL(FACE_STATE_DOWN, g_state.snapshot().face_state);
    TEST_ASSERT_EQUAL(PAGE_SLEEP_FACE_DOWN, g_state.snapshot().page);

    g_mock_millis += 100;
    g_state.updateFace(GESTURE_FACE_UP_OPEN, g_mock_millis);
    const StateSnapshot& s = g_state.snapshot();
    TEST_ASSERT_EQUAL(FACE_STATE_UP, s.face_state);
    TEST_ASSERT_EQUAL(ORIENTATION_PORTRAIT, s.orientation);
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_AI_USAGE, s.page);
}

void test_short_press_buttons_do_not_switch_pages_in_gesture_first_mode() {
    g_state.updateButton(BUTTON_NEXT, g_mock_millis);
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_OVERVIEW, g_state.snapshot().page);

    g_state.updateGesture(GESTURE_SHAKE_RIGHT, g_mock_millis + 100);
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_AI_USAGE, g_state.snapshot().page);

    g_state.updateButton(BUTTON_PREV, g_mock_millis + 200);
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_AI_USAGE, g_state.snapshot().page);
}

void test_settings_uses_a_to_select_and_b_to_cycle_current_value() {
    g_state.forcePage(PAGE_PORTRAIT_SETTINGS);

    g_state.updateButton(BUTTON_NEXT, g_mock_millis);
    TEST_ASSERT_EQUAL_UINT8(1, g_state.snapshot().settingsSelectedIndex);

    const uint8_t before = g_state.snapshot().settingsValues[1];
    g_state.updateButton(BUTTON_PREV, g_mock_millis + 100);
    TEST_ASSERT_NOT_EQUAL(before, g_state.snapshot().settingsValues[1]);
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_SETTINGS, g_state.snapshot().page);
}

void test_what2eat_b_short_picks_once_and_b_long_keeps_back_behavior() {
    g_state.forcePage(PAGE_PORTRAIT_WHAT2EAT);
    g_state.forceSystem(SYSTEM_AMBIENT);
    g_state.updateButton(BUTTON_PREV, g_mock_millis);
    TEST_ASSERT_EQUAL_INT(1, g_test_what2eat_pick_calls);
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_WHAT2EAT, g_state.snapshot().page);
    TEST_ASSERT_EQUAL(SYSTEM_ACTIVE, g_state.snapshot().system);

    g_state.updateButton(BUTTON_BACK, g_mock_millis + 100);
    TEST_ASSERT_EQUAL_INT(1, g_test_what2eat_pick_calls);
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_OVERVIEW, g_state.snapshot().page);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_runtime_rotate_to_landscape_is_ignored_for_current_mvp);
    RUN_TEST(test_runtime_orientation_landscape_detection_is_ignored_for_current_mvp);
    RUN_TEST(test_face_down_and_face_up_restore_portrait_page);
    RUN_TEST(test_short_press_buttons_do_not_switch_pages_in_gesture_first_mode);
    RUN_TEST(test_settings_uses_a_to_select_and_b_to_cycle_current_value);
    RUN_TEST(test_what2eat_b_short_picks_once_and_b_long_keeps_back_behavior);
    return UNITY_END();
}
