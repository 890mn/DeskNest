// src/ai_usage_module.h
// 栖屏 DeskNest - AI usage status module
//
// 当前阶段提供 demo/cache snapshot；后续接 cc-switch/status.json 时，
// UI model 不需要改，只替换模块数据来源。

#ifndef DESKNEST_AI_USAGE_MODULE_H
#define DESKNEST_AI_USAGE_MODULE_H

#include <stdint.h>

namespace desknest {

struct AIUsageItemStatus {
    const char* name = "";
    uint8_t percent = 0;
    const char* statusText = "";
    const char* detailText = "";
};

struct AIUsageStatus {
    uint8_t totalPercent = 0;
    AIUsageItemStatus minimax;
    AIUsageItemStatus codex;
    AIUsageItemStatus chatgpt;
    const char* updatedAtText = "";
    const char* warningText = "";
};

inline uint8_t dn_clamp_percent(int value) {
    if (value < 0) return 0;
    if (value > 100) return 100;
    return (uint8_t)value;
}

inline AIUsageItemStatus dn_ai_usage_item(const char* name,
                                          int percent,
                                          const char* statusText,
                                          const char* detailText) {
    AIUsageItemStatus item;
    item.name = name;
    item.percent = dn_clamp_percent(percent);
    item.statusText = statusText;
    item.detailText = detailText;
    return item;
}

inline AIUsageStatus dn_ai_usage_demo_status() {
    AIUsageStatus status;
    status.totalPercent = 72;
    status.chatgpt = dn_ai_usage_item("ChatGPT", 72, "OK", "via cc-switch");
    status.codex = dn_ai_usage_item("Codex", 58, "正常", "");
    status.minimax = dn_ai_usage_item("MiniMax", 86, "充足", "");
    status.updatedAtText = "cached";
    status.warningText = "";
    return status;
}

} // namespace desknest

#endif // DESKNEST_AI_USAGE_MODULE_H

