#include <unity.h>

#include "../../src/ui_model.h"

using namespace desknest;

static StateSnapshot baseSnapshot() {
    StateSnapshot s = {};
    s.system = SYSTEM_ACTIVE;
    s.face_state = FACE_STATE_UP;
    s.orientation = ORIENTATION_PORTRAIT;
    s.page = PAGE_PORTRAIT_OVERVIEW;
    s.rotLock = ROT_AUTO;
    s.lastInputMs = 1000;
    s.pre_face_down_page = PAGE_PORTRAIT_OVERVIEW;
    s.last_portrait_page = PAGE_PORTRAIT_OVERVIEW;
    s.last_landscape_page = PAGE_LANDSCAPE_FOCUS;
    return s;
}

void test_ui_model_maps_overview_sensor_values() {
    UiModelInputs in = {};
    in.state = baseSnapshot();
    in.nowMs = 42500;
    in.temperatureValid = true;
    in.temperatureC = 23.5f;
    in.humidityPct = 58.0f;
    in.luxValid = true;
    in.lux = 240;
    in.batteryValid = false;

    UiModel model = dn_build_ui_model_from_inputs(in);

    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_OVERVIEW, model.view.page);
    TEST_ASSERT_EQUAL(ORIENTATION_PORTRAIT, model.view.orientation);
    TEST_ASSERT_EQUAL_UINT32(41, model.view.idleSeconds);
    TEST_ASSERT_TRUE(model.overview.environmentValid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.5f, model.overview.temperatureC);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 58.0f, model.overview.humidityPct);
    TEST_ASSERT_EQUAL_UINT16(240, model.overview.lux);
    TEST_ASSERT_EQUAL_STRING("Portrait", model.status.orientationText);
    TEST_ASSERT_EQUAL_STRING("ACTIVE", model.status.systemText);
}

void test_ui_model_marks_face_down_as_sleeping_special_page() {
    UiModelInputs in = {};
    in.state = baseSnapshot();
    in.state.system = SYSTEM_FACE_DOWN_SLEEP;
    in.state.face_state = FACE_STATE_DOWN;
    in.state.orientation = ORIENTATION_FACE_DOWN;
    in.state.page = PAGE_SLEEP_FACE_DOWN;
    in.nowMs = 3000;

    UiModel model = dn_build_ui_model_from_inputs(in);

    TEST_ASSERT_TRUE(model.view.isFaceDown);
    TEST_ASSERT_TRUE(model.view.isSleeping);
    TEST_ASSERT_EQUAL(PAGE_SLEEP_FACE_DOWN, model.view.page);
    TEST_ASSERT_EQUAL_STRING("栖于桌面", model.faceDown.line1);
    TEST_ASSERT_EQUAL_STRING("息于常亮之间", model.faceDown.line2);
}

void test_ui_model_maps_shake_animation() {
    UiModelInputs in = {};
    in.state = baseSnapshot();
    in.nowMs = 1000;
    in.shakePhase = SHAKE_PHASE_RETURNING;
    in.shakeDirection = 1;

    UiModel model = dn_build_ui_model_from_inputs(in);

    TEST_ASSERT_EQUAL(SHAKE_VISUAL_RETURNING, model.animation.shakePhase);
    TEST_ASSERT_EQUAL_INT8(1, model.animation.shakeDirection);
    TEST_ASSERT_EQUAL_UINT8(70, model.animation.shakeProgressPct);
}

void test_home_focus_prioritizes_ai_risk_over_life() {
    AIUsageStatus ai = {};
    ai.totalPercent = 82;
    UiHomeFocusProps focus = dn_resolve_home_focus(ai, true);
    TEST_ASSERT_EQUAL(HOME_FOCUS_AI_RISK, focus.kind);
    TEST_ASSERT_TRUE(focus.actionable);
}

void test_home_focus_uses_life_reminder_when_ai_is_normal() {
    AIUsageStatus ai = {};
    ai.totalPercent = 42;
    UiHomeFocusProps focus = dn_resolve_home_focus(ai, true);
    TEST_ASSERT_EQUAL(HOME_FOCUS_LIFE_REMINDER, focus.kind);
    TEST_ASSERT_EQUAL_UINT8(60, focus.priority);
}

void test_home_focus_falls_back_to_default_summary() {
    AIUsageStatus ai = {};
    ai.totalPercent = 42;
    UiHomeFocusProps focus = dn_resolve_home_focus(ai, false);
    TEST_ASSERT_EQUAL(HOME_FOCUS_DEFAULT, focus.kind);
    TEST_ASSERT_FALSE(focus.actionable);
}

void test_home_focus_honors_manual_life_module_selection() {
    UiModelInputs in = {};
    in.state = baseSnapshot();
    in.state.settingsValues[0] = 2;

    UiModel model = dn_build_ui_model_from_inputs(in);
    TEST_ASSERT_EQUAL(HOME_FOCUS_LIFE_REMINDER, model.homeFocus.kind);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ui_model_maps_overview_sensor_values);
    RUN_TEST(test_ui_model_marks_face_down_as_sleeping_special_page);
    RUN_TEST(test_ui_model_maps_shake_animation);
    RUN_TEST(test_home_focus_prioritizes_ai_risk_over_life);
    RUN_TEST(test_home_focus_uses_life_reminder_when_ai_is_normal);
    RUN_TEST(test_home_focus_falls_back_to_default_summary);
    RUN_TEST(test_home_focus_honors_manual_life_module_selection);
    return UNITY_END();
}
