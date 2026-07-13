#include <unity.h>

#include "../../src/ai_usage_module.h"

using namespace desknest;

void test_ai_usage_module_returns_demo_snapshot() {
    AIUsageStatus status = dn_ai_usage_demo_status();

    TEST_ASSERT_EQUAL_UINT8(72, status.totalPercent);
    TEST_ASSERT_EQUAL_STRING("ChatGPT", status.chatgpt.name);
    TEST_ASSERT_EQUAL_UINT8(72, status.chatgpt.percent);
    TEST_ASSERT_EQUAL_UINT8(11, status.chatgpt.weeklyPercent);
    TEST_ASSERT_EQUAL_STRING("Codex", status.codex.name);
    TEST_ASSERT_EQUAL_UINT8(58, status.codex.percent);
    TEST_ASSERT_EQUAL_STRING("MiniMax", status.minimax.name);
    TEST_ASSERT_EQUAL_UINT8(86, status.minimax.percent);
    TEST_ASSERT_EQUAL_UINT8(18, status.minimax.weeklyPercent);
    TEST_ASSERT_EQUAL_STRING("cached", status.updatedAtText);
}

void test_ai_usage_module_clamps_percent() {
    TEST_ASSERT_EQUAL_UINT8(0, dn_clamp_percent(-5));
    TEST_ASSERT_EQUAL_UINT8(50, dn_clamp_percent(50));
    TEST_ASSERT_EQUAL_UINT8(100, dn_clamp_percent(130));
}

void test_ai_usage_module_parses_cc_switch_cache() {
    const char* json =
        "{"
        "\"updatedAtText\":\"12 min\","
        "\"serverNow\":\"2026-07-09T06:04:34+08:00\","
        "\"nextRefreshInSec\":17,"
        "\"totalPercent\":64,"
        "\"chatgpt\":{\"percent\":64,\"status\":\"Plus\",\"detail\":\"5h:64% wk:21%\",\"fiveHourExpireAt\":\"2026-07-09T06:26:18+08:00\",\"weekExpireAt\":\"2026-07-14T04:43:08+08:00\"},"
        "\"minimax\":{\"percent\":28,\"weeklyPercent\":35,\"status\":\"Token\",\"detail\":\"1.2M left\",\"fiveHourExpireAt\":\"2026-07-09T07:00:03+08:00\",\"weekExpireAt\":\"2026-07-12T16:00:03+08:00\"},"
        "\"codexResets\":[{\"name\":\"Codex RE1\",\"expireAt\":\"2001-07-18T08:10:00+08:00\"},{\"name\":\"Codex RE2\",\"expireAt\":\"2001-07-27T07:44:00+08:00\"}]"
        "}";
    AIUsageParseStorage storage;
    AIUsageStatus status;

    TEST_ASSERT_TRUE(dn_ai_usage_parse_cc_switch_status(json, &storage, &status));
    TEST_ASSERT_TRUE(status.fromCache);
    TEST_ASSERT_EQUAL_UINT8(64, status.totalPercent);
    TEST_ASSERT_EQUAL_STRING("12 min", status.updatedAtText);
    TEST_ASSERT_EQUAL_STRING("2026-07-09T06:04:34+08:00", status.serverNow);
    TEST_ASSERT_EQUAL_UINT16(17, status.nextRefreshInSec);
    TEST_ASSERT_EQUAL_STRING("ChatGPT", status.chatgpt.name);
    TEST_ASSERT_EQUAL_UINT8(64, status.chatgpt.percent);
    TEST_ASSERT_EQUAL_UINT8(21, status.chatgpt.weeklyPercent);
    TEST_ASSERT_EQUAL_STRING("5h:64% wk:21%", status.chatgpt.detailText);
    TEST_ASSERT_EQUAL_STRING("2026-07-09T06:26:18+08:00", status.chatgpt.fiveHourExpireAt);
    TEST_ASSERT_EQUAL_STRING("2026-07-14T04:43:08+08:00", status.chatgpt.weekExpireAt);
    TEST_ASSERT_EQUAL_STRING("MiniMax", status.minimax.name);
    TEST_ASSERT_EQUAL_UINT8(28, status.minimax.percent);
    TEST_ASSERT_EQUAL_UINT8(35, status.minimax.weeklyPercent);
    TEST_ASSERT_EQUAL_STRING("1.2M left", status.minimax.detailText);
    TEST_ASSERT_EQUAL_STRING("2026-07-09T07:00:03+08:00", status.minimax.fiveHourExpireAt);
    TEST_ASSERT_EQUAL_STRING("2026-07-12T16:00:03+08:00", status.minimax.weekExpireAt);
    TEST_ASSERT_EQUAL_UINT8(2, status.codexResetCount);
    TEST_ASSERT_EQUAL_STRING("Codex RE1", status.codexResets[0].name);
    TEST_ASSERT_EQUAL_STRING("2001-07-18T08:10:00+08:00", status.codexResets[0].expireAt);
    TEST_ASSERT_EQUAL_STRING("Codex RE2", status.codexResets[1].name);
    TEST_ASSERT_EQUAL_STRING("2001-07-27T07:44:00+08:00", status.codexResets[1].expireAt);
}

void test_ai_usage_module_uses_max_service_percent_without_total() {
    const char* json =
        "{"
        "\"updatedAt\":\"now\","
        "\"chatgpt\":{\"percent\":22},"
        "\"minimax\":{\"percent\":77}"
        "}";
    AIUsageParseStorage storage;
    AIUsageStatus status;

    TEST_ASSERT_TRUE(dn_ai_usage_parse_cc_switch_status(json, &storage, &status));
    TEST_ASSERT_EQUAL_UINT8(77, status.totalPercent);
    TEST_ASSERT_EQUAL_STRING("now", status.updatedAtText);
}

void test_ai_usage_module_weekly_only_uses_weekly_effective_percent() {
    const char* json =
        "{"
        "\"chatgpt\":{\"percent\":0,\"weeklyPercent\":39,\"fiveHourAvailable\":false,\"weeklyAvailable\":true},"
        "\"minimax\":{\"percent\":1,\"fiveHourAvailable\":true,\"weeklyAvailable\":true}"
        "}";
    AIUsageParseStorage storage;
    AIUsageStatus status;

    TEST_ASSERT_TRUE(dn_ai_usage_parse_cc_switch_status(json, &storage, &status));
    TEST_ASSERT_FALSE(status.chatgpt.fiveHourAvailable);
    TEST_ASSERT_TRUE(status.chatgpt.weeklyAvailable);
    TEST_ASSERT_EQUAL_UINT8(39, status.chatgpt.effectivePercent);
    TEST_ASSERT_EQUAL_UINT8(39, status.totalPercent);
}

void test_ai_usage_module_distinguishes_real_zero_five_hour_quota() {
    const char* json =
        "{"
        "\"chatgpt\":{\"percent\":0,\"weeklyPercent\":0,\"fiveHourAvailable\":true,\"weeklyAvailable\":true}"
        "}";
    AIUsageParseStorage storage;
    AIUsageStatus status;

    TEST_ASSERT_TRUE(dn_ai_usage_parse_cc_switch_status(json, &storage, &status));
    TEST_ASSERT_TRUE(status.chatgpt.fiveHourAvailable);
    TEST_ASSERT_EQUAL_UINT8(0, status.chatgpt.effectivePercent);
}

void test_ai_usage_module_parses_server_now_epoch() {
    const time_t epoch = dn_parse_iso8601_epoch("2026-07-09T06:04:34+08:00");
    TEST_ASSERT_EQUAL_INT64(1783548274LL, (long long)epoch);
}

void test_ai_usage_module_applies_server_now_plus8_correction() {
    const time_t epoch = dn_parse_iso8601_epoch("2026-07-09T06:04:34+08:00");
    const time_t corrected = dn_apply_server_now_boot_offset(epoch);
    TEST_ASSERT_EQUAL_INT64(1783577074LL, (long long)corrected);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ai_usage_module_returns_demo_snapshot);
    RUN_TEST(test_ai_usage_module_clamps_percent);
    RUN_TEST(test_ai_usage_module_parses_cc_switch_cache);
    RUN_TEST(test_ai_usage_module_uses_max_service_percent_without_total);
    RUN_TEST(test_ai_usage_module_weekly_only_uses_weekly_effective_percent);
    RUN_TEST(test_ai_usage_module_distinguishes_real_zero_five_hour_quota);
    RUN_TEST(test_ai_usage_module_parses_server_now_epoch);
    RUN_TEST(test_ai_usage_module_applies_server_now_plus8_correction);
    return UNITY_END();
}
