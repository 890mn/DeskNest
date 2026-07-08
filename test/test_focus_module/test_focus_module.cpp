#include <unity.h>

#include "../../src/focus_module.h"

using namespace desknest;

void test_focus_module_returns_default_session() {
    FocusStatus status = dn_focus_default_status();

    TEST_ASSERT_EQUAL_STRING("DEEP WORK", status.modeText);
    TEST_ASSERT_EQUAL_STRING("25:00", status.timerText);
    TEST_ASSERT_EQUAL(FOCUS_RUNNING, status.state);
    TEST_ASSERT_EQUAL_STRING("> IN PROGRESS", status.stateText);
    TEST_ASSERT_EQUAL_STRING("Goal · 50 min", status.goalText);
}

void test_focus_module_formats_state_text() {
    TEST_ASSERT_EQUAL_STRING("IDLE", dn_focus_state_text(FOCUS_IDLE));
    TEST_ASSERT_EQUAL_STRING("> IN PROGRESS", dn_focus_state_text(FOCUS_RUNNING));
    TEST_ASSERT_EQUAL_STRING("PAUSED", dn_focus_state_text(FOCUS_PAUSED));
    TEST_ASSERT_EQUAL_STRING("DONE", dn_focus_state_text(FOCUS_DONE));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_focus_module_returns_default_session);
    RUN_TEST(test_focus_module_formats_state_text);
    return UNITY_END();
}

