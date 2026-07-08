#include <unity.h>

#include "../../src/ai_usage_module.h"

using namespace desknest;

void test_ai_usage_module_returns_demo_snapshot() {
    AIUsageStatus status = dn_ai_usage_demo_status();

    TEST_ASSERT_EQUAL_UINT8(72, status.totalPercent);
    TEST_ASSERT_EQUAL_STRING("ChatGPT", status.chatgpt.name);
    TEST_ASSERT_EQUAL_UINT8(72, status.chatgpt.percent);
    TEST_ASSERT_EQUAL_STRING("Codex", status.codex.name);
    TEST_ASSERT_EQUAL_UINT8(58, status.codex.percent);
    TEST_ASSERT_EQUAL_STRING("MiniMax", status.minimax.name);
    TEST_ASSERT_EQUAL_UINT8(86, status.minimax.percent);
    TEST_ASSERT_EQUAL_STRING("cached", status.updatedAtText);
}

void test_ai_usage_module_clamps_percent() {
    TEST_ASSERT_EQUAL_UINT8(0, dn_clamp_percent(-5));
    TEST_ASSERT_EQUAL_UINT8(50, dn_clamp_percent(50));
    TEST_ASSERT_EQUAL_UINT8(100, dn_clamp_percent(130));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ai_usage_module_returns_demo_snapshot);
    RUN_TEST(test_ai_usage_module_clamps_percent);
    return UNITY_END();
}

