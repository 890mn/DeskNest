// src/ui_model.h
// 栖屏 DeskNest - UI Contract runtime model
//
// 这是架构层与 UI 层之间的过渡契约：
// - UI renderer 消费 UiModel；
// - 业务/状态/传感器数据由 builder 组装；
// - 纯 builder 可在 host 上测试。

#ifndef DESKNEST_UI_MODEL_H
#define DESKNEST_UI_MODEL_H

#include "state_machine.h"
#include "gesture.h"
#include "page_registry.h"
#include "environment_module.h"
#include "ai_usage_module.h"
#include "focus_module.h"
#include "settings_module.h"

#include <stdint.h>

namespace desknest {

enum ScreenMode : uint8_t {
    SCREEN_PORTRAIT_OVERVIEW = 0,
    SCREEN_PORTRAIT_DETAIL,
    SCREEN_LANDSCAPE_OVERVIEW,
    SCREEN_LANDSCAPE_FOCUS,
    SCREEN_LANDSCAPE_CUSTOM,
    SCREEN_FACE_DOWN,
    SCREEN_CONFIG,
};

enum ShakeVisualPhase : uint8_t {
    SHAKE_VISUAL_IDLE = 0,
    SHAKE_VISUAL_OUTBOUND,
    SHAKE_VISUAL_RETURNING,
};

enum UiWiFiState : uint8_t {
    UI_WIFI_UNCONFIGURED = 0,
    UI_WIFI_DISCONNECTED,
    UI_WIFI_CONNECTING,
    UI_WIFI_CONNECTED,
    UI_WIFI_NO_SSID,
    UI_WIFI_AUTH_FAILED,
};

struct UiViewState {
    ScreenMode mode = SCREEN_PORTRAIT_OVERVIEW;
    UIPage page = PAGE_PORTRAIT_OVERVIEW;
    OrientationState orientation = ORIENTATION_PORTRAIT;
    SystemState system = SYSTEM_ACTIVE;

    bool isAmbient = false;
    bool isFaceDown = false;
    bool isSleeping = false;
    bool isConfig = false;

    uint32_t nowMs = 0;
    uint32_t idleSeconds = 0;
};

struct UiHeaderProps {
    const char* title = "";
    const char* subtitle = "";
    uint8_t pageIndex = 0;
    uint8_t pageCount = 0;
    bool showHeader = true;
    bool showPageDots = true;
};

struct UiFooterProps {
    const char* leftHint = "";
    const char* rightHint = "";
    const char* statusText = "";
    bool showFooter = true;
    bool showIdle = true;
};

struct UiNavigationProps {
    bool canNext = false;
    bool canPrev = false;
    bool canSelect = false;
    bool canBack = false;
    const char* nextLabel = "";
    const char* prevLabel = "";
    const char* selectLabel = "";
    const char* backLabel = "";
};

struct UiStatusProps {
    const char* systemText = "";
    const char* orientationText = "";
    const char* wifiText = "";
    const char* syncText = "";
    UiWiFiState wifiState = UI_WIFI_UNCONFIGURED;
    uint8_t batteryPercent = 0;
    bool batteryValid = false;
    bool charging = false;
    bool warning = false;
};

struct UiOverviewProps {
    const char* timeText = "";
    uint8_t aiTotalPercent = 0;
    const char* aiStatusText = "";

    float temperatureC = 0.0f;
    float humidityPct = 0.0f;
    uint16_t lux = 0;
    bool environmentValid = false;

    const char* comfortText = "";
    const char* suggestionText = "";
    const char* messageText = "";
    const char* updatedAtText = "";
};

struct UiUsageItemProps {
    const char* name = "";
    uint8_t percent = 0;
    uint8_t weeklyPercent = 0;
    const char* statusText = "";
    const char* detailText = "";
    const char* fiveHourExpireAt = "";
    const char* weekExpireAt = "";
};

struct UiCodexResetProps {
    const char* name = "";
    const char* expireAt = "";
};

struct UiAiUsageProps {
    uint8_t totalPercent = 0;
    UiUsageItemProps minimax;
    UiUsageItemProps codex;
    UiUsageItemProps chatgpt;
    UiCodexResetProps codexResets[4];
    uint8_t codexResetCount = 0;
    const char* updatedAtText = "";
    const char* warningText = "";
    uint16_t nextRefreshInSec = 0;
};

struct UiEnvironmentProps {
    float temperatureC = 0.0f;
    float humidityPct = 0.0f;
    uint16_t lux = 0;
    bool valid = false;

    uint8_t score = 0;
    const char* gradeText = "";
    const char* temperatureGrade = "";
    const char* humidityGrade = "";
    const char* lightGrade = "";
    const char* adviceText = "";
};

struct UiSettingsRowProps {
    const char* label = "";
    const char* value = "";
    bool selectable = false;
};

struct UiSettingsProps {
    UiSettingsRowProps rows[8];
    uint8_t rowCount = 0;
    uint8_t selectedIndex = 0;
    const char* dangerHint = "";
};

struct UiLandscapeOverviewProps {
    uint8_t aiTotalPercent = 0;
    const char* minimaxText = "";
    const char* codexText = "";
    float temperatureC = 0.0f;
    float humidityPct = 0.0f;
    uint16_t lux = 0;
    bool environmentValid = false;
    const char* systemText = "";
};

struct UiFocusProps {
    const char* modeText = "";
    const char* timerText = "";
    FocusState state = FOCUS_IDLE;
    const char* stateText = "";
    const char* goalText = "";
    uint8_t aiTotalPercent = 0;
    const char* environmentText = "";
};

struct UiCustomCardProps {
    const char* label = "";
    const char* value = "";
    bool active = false;
};

struct UiCustomProps {
    UiCustomCardProps cards[4];
    uint8_t cardCount = 0;
    const char* hintText = "";
};

struct UiMenuCandidateProps {
    const char* name = "";        // "番茄牛腩面"
    const char* price = "";       // "¥28"  （已含符号）
    uint8_t score = 0;            // 0-100，UI 显示 ÷10
    bool active = false;          // 今日推荐（数据层决定）
};

struct UiMenuGroupProps {
    const char* name = "";        // "面食"
    UiMenuCandidateProps candidates[6];
    uint8_t candidateCount = 0;
};

struct UiMenuProps {
    const char* ask = "";         // "今天，吃点热的？"
    const char* lastMeal = "";    // "昨天 · 日式咖喱饭"
    UiMenuGroupProps groups[4];
    uint8_t groupCount = 0;
    const char* diceHint = "";    // "[A] 重抽 · [B] 记下"
};

struct UiFaceDownProps {
    const char* line1 = "";
    const char* line2 = "";
    const char* line3 = "";
    bool showBreathingDot = true;
};

struct UiConfigProps {
    const char* ssidText = "";
    const char* urlText = "";
    const char* stepText = "";
    const char* hintText = "";
};

struct UiAnimationProps {
    ShakeVisualPhase shakePhase = SHAKE_VISUAL_IDLE;
    int8_t shakeDirection = 0;
    uint8_t shakeProgressPct = 0;
    bool pageChanged = false;
    bool forceFullRedraw = false;
};

struct UiModel {
    UiViewState view;
    UiHeaderProps header;
    UiFooterProps footer;
    UiNavigationProps nav;
    UiStatusProps status;

    UiOverviewProps overview;
    UiAiUsageProps aiUsage;
    UiMenuProps menu;
    UiEnvironmentProps environment;
    UiSettingsProps settings;
    UiLandscapeOverviewProps landscapeOverview;
    UiFocusProps focus;
    UiCustomProps customPage;
    UiFaceDownProps faceDown;
    UiConfigProps config;

    UiAnimationProps animation;
};

struct UiModelInputs {
    StateSnapshot state = {};
    uint32_t nowMs = 0;

    bool temperatureValid = false;
    float temperatureC = 0.0f;
    float humidityPct = 0.0f;

    bool luxValid = false;
    uint16_t lux = 0;

    bool batteryValid = false;
    uint8_t batteryPercent = 0;
    bool charging = false;

    ShakePhase shakePhase = SHAKE_PHASE_IDLE;
    int8_t shakeDirection = 0;
};

inline const char* dn_system_text(SystemState s) {
    switch (s) {
        case SYSTEM_ACTIVE:          return "ACTIVE";
        case SYSTEM_AMBIENT:         return "AMBIENT";
        case SYSTEM_LIGHT_SLEEP:     return "SLEEP";
        case SYSTEM_FACE_DOWN_SLEEP: return "FACE_DN";
        case SYSTEM_CONFIG:          return "CONFIG";
        case SYSTEM_BOOT:            return "BOOT";
        default:                     return "?";
    }
}

inline const char* dn_orientation_text(OrientationState o) {
    switch (o) {
        case ORIENTATION_PORTRAIT:  return "Portrait";
        case ORIENTATION_LANDSCAPE: return "Landscape";
        case ORIENTATION_FACE_DOWN: return "Face-Dn";
        default:                    return "Unknown";
    }
}

inline ScreenMode dn_screen_mode_for_page(UIPage p) {
    switch (p) {
        case PAGE_PORTRAIT_OVERVIEW:    return SCREEN_PORTRAIT_OVERVIEW;
        case PAGE_PORTRAIT_AI_USAGE:
        case PAGE_PORTRAIT_MENU:
        case PAGE_PORTRAIT_ENVIRONMENT:
        case PAGE_PORTRAIT_SETTINGS:    return SCREEN_PORTRAIT_DETAIL;
        case PAGE_LANDSCAPE_OVERVIEW:   return SCREEN_LANDSCAPE_OVERVIEW;
        case PAGE_LANDSCAPE_FOCUS:      return SCREEN_LANDSCAPE_FOCUS;
        case PAGE_LANDSCAPE_CUSTOM:     return SCREEN_LANDSCAPE_CUSTOM;
        case PAGE_SLEEP_FACE_DOWN:      return SCREEN_FACE_DOWN;
        case PAGE_CONFIG_PORTAL:        return SCREEN_CONFIG;
        default:                        return SCREEN_PORTRAIT_OVERVIEW;
    }
}

inline const char* dn_page_title(UIPage p) {
    return dn_page_title_from_registry(p);
}

inline uint8_t dn_page_index_in_group(UIPage p) {
    const PageDef* def = dn_find_page_def(p);
    if (!def) return 0;
    const int8_t index = dn_index_in_group(def->group, p);
    if (index >= 0) return (uint8_t)index;
    return 0;
}

inline uint8_t dn_page_count_in_group(UIPage p) {
    const PageDef* def = dn_find_page_def(p);
    if (!def || def->group == PAGE_GROUP_SPECIAL) return 0;
    return dn_page_group_count(def->group);
}

inline ShakeVisualPhase dn_shake_visual_phase(ShakePhase phase) {
    switch (phase) {
        case SHAKE_PHASE_OUTBOUND:  return SHAKE_VISUAL_OUTBOUND;
        case SHAKE_PHASE_RETURNING: return SHAKE_VISUAL_RETURNING;
        default:                    return SHAKE_VISUAL_IDLE;
    }
}

inline UiUsageItemProps dn_usage_item(const AIUsageItemStatus& src) {
    UiUsageItemProps item;
    item.name = src.name;
    item.percent = src.percent;
    item.weeklyPercent = src.weeklyPercent;
    item.statusText = src.statusText;
    item.detailText = src.detailText;
    item.fiveHourExpireAt = src.fiveHourExpireAt;
    item.weekExpireAt = src.weekExpireAt;
    return item;
}

inline UiSettingsRowProps dn_settings_row(const SettingsRowStatus& src) {
    UiSettingsRowProps row;
    row.label = src.label;
    row.value = src.value;
    row.selectable = src.selectable;
    return row;
}

inline UiCustomCardProps dn_custom_card(const char* label,
                                        const char* value,
                                        bool active) {
    UiCustomCardProps card;
    card.label = label;
    card.value = value;
    card.active = active;
    return card;
}

inline UiModel dn_build_ui_model_from_inputs(const UiModelInputs& in) {
    UiModel m = {};
    const StateSnapshot& s = in.state;

    m.view.mode = dn_screen_mode_for_page(s.page);
    m.view.page = s.page;
    m.view.orientation = s.orientation;
    m.view.system = s.system;
    m.view.isAmbient = (s.system == SYSTEM_AMBIENT);
    m.view.isFaceDown = (s.face_state == FACE_STATE_DOWN ||
                         s.system == SYSTEM_FACE_DOWN_SLEEP ||
                         s.page == PAGE_SLEEP_FACE_DOWN);
    m.view.isSleeping = (s.system == SYSTEM_LIGHT_SLEEP ||
                         s.system == SYSTEM_FACE_DOWN_SLEEP);
    m.view.isConfig = (s.system == SYSTEM_CONFIG ||
                       s.page == PAGE_CONFIG_PORTAL);
    m.view.nowMs = in.nowMs;
    m.view.idleSeconds = (in.nowMs >= s.lastInputMs)
        ? ((in.nowMs - s.lastInputMs) / 1000)
        : 0;

    m.header.title = dn_page_title(s.page);
    m.header.pageIndex = dn_page_index_in_group(s.page);
    m.header.pageCount = dn_page_count_in_group(s.page);
    m.header.showHeader = !m.view.isFaceDown;
    m.header.showPageDots = (m.header.pageCount > 0);

    m.footer.showFooter = !m.view.isFaceDown;
    m.footer.showIdle = !m.view.isFaceDown;
    m.footer.leftHint = "[A] Next";
    m.footer.rightHint = "[B] Prev";

    m.nav.canNext = !m.view.isFaceDown;
    m.nav.canPrev = !m.view.isFaceDown;
    m.nav.canBack = !m.view.isFaceDown;
    m.nav.canSelect = !m.view.isFaceDown;
    m.nav.nextLabel = "Next";
    m.nav.prevLabel = "Prev";
    m.nav.backLabel = "Back";
    m.nav.selectLabel = "Select";

    m.status.systemText = dn_system_text(s.system);
    m.status.orientationText = dn_orientation_text(s.orientation);
    m.status.wifiText = "WiFi --";
    m.status.syncText = "sync --";
    m.status.batteryValid = in.batteryValid;
    m.status.batteryPercent = in.batteryPercent;
    m.status.charging = in.charging;
    m.status.warning = false;

    m.overview.aiTotalPercent = 72;
    m.overview.aiStatusText = "Mock";
    m.overview.temperatureC = in.temperatureC;
    m.overview.humidityPct = in.humidityPct;
    m.overview.lux = in.lux;
    m.overview.environmentValid = in.temperatureValid;
    m.overview.comfortText = "良好";
    m.overview.suggestionText = "保持专注";
    m.overview.messageText = "今天额度还够";
    m.overview.updatedAtText = "cached";

    const AIUsageStatus aiStatus = dn_ai_usage_demo_status();
    m.aiUsage.totalPercent = aiStatus.totalPercent;
    m.aiUsage.chatgpt = dn_usage_item(aiStatus.chatgpt);
    m.aiUsage.codex = dn_usage_item(aiStatus.codex);
    m.aiUsage.minimax = dn_usage_item(aiStatus.minimax);
    m.aiUsage.updatedAtText = aiStatus.updatedAtText;
    m.aiUsage.warningText = aiStatus.warningText;
    m.aiUsage.nextRefreshInSec = aiStatus.nextRefreshInSec;
    m.overview.aiTotalPercent = aiStatus.totalPercent;

    EnvironmentInput envIn;
    envIn.valid = in.temperatureValid;
    envIn.temperatureC = in.temperatureC;
    envIn.humidityPct = in.humidityPct;
    envIn.lux = in.lux;
    const EnvironmentStatus envStatus = dn_evaluate_environment(envIn);
    m.environment.temperatureC = in.temperatureC;
    m.environment.humidityPct = in.humidityPct;
    m.environment.lux = in.lux;
    m.environment.valid = envStatus.valid;
    m.environment.score = envStatus.score;
    m.environment.gradeText = envStatus.gradeText;
    m.environment.temperatureGrade = envStatus.temperatureGrade;
    m.environment.humidityGrade = envStatus.humidityGrade;
    m.environment.lightGrade = envStatus.lightGrade;
    m.environment.adviceText = envStatus.adviceText;

    const SettingsStatus settingsStatus = dn_settings_default_status();
    m.settings.rowCount = settingsStatus.rowCount;
    for (uint8_t i = 0; i < settingsStatus.rowCount && i < 8; ++i) {
        m.settings.rows[i] = dn_settings_row(settingsStatus.rows[i]);
    }
    m.settings.selectedIndex = settingsStatus.selectedIndex;
    m.settings.dangerHint = settingsStatus.dangerHint;

    m.landscapeOverview.aiTotalPercent = m.aiUsage.totalPercent;
    m.landscapeOverview.minimaxText = "MiniMax OK";
    m.landscapeOverview.codexText = "Codex OK";
    m.landscapeOverview.temperatureC = in.temperatureC;
    m.landscapeOverview.humidityPct = in.humidityPct;
    m.landscapeOverview.lux = in.lux;
    m.landscapeOverview.environmentValid = in.temperatureValid;
    m.landscapeOverview.systemText = m.status.systemText;

    const FocusStatus focusStatus = dn_focus_default_status();
    m.focus.modeText = focusStatus.modeText;
    m.focus.timerText = focusStatus.timerText;
    m.focus.state = focusStatus.state;
    m.focus.stateText = focusStatus.stateText;
    m.focus.goalText = focusStatus.goalText;
    m.focus.aiTotalPercent = m.aiUsage.totalPercent;
    m.focus.environmentText = m.environment.gradeText;

    m.customPage.cardCount = 3;
    m.customPage.cards[0] = dn_custom_card("WORK", "", false);
    m.customPage.cards[1] = dn_custom_card("REST", "", false);
    m.customPage.cards[2] = dn_custom_card("MEET", "", false);
    m.customPage.hintText = "Tap to switch mode";

    m.faceDown.line1 = "栖于桌面";
    m.faceDown.line2 = "息于常亮之间";
    m.faceDown.line3 = "DeskNest";
    m.faceDown.showBreathingDot = true;

    m.config.ssidText = "DeskNest-XXXX";
    m.config.urlText = "192.168.4.1";
    m.config.stepText = "3. Setup & save";
    m.config.hintText = "Restart to apply";

    m.animation.shakePhase = dn_shake_visual_phase(in.shakePhase);
    m.animation.shakeDirection = in.shakeDirection;
    m.animation.shakeProgressPct = shakeAnimationPercent(in.shakePhase);
    m.animation.pageChanged = false;
    m.animation.forceFullRedraw = false;

    // ---- 今天吃什么（MenuModule mock） ----
    m.menu.ask = "今天，吃点热的？";
    m.menu.lastMeal = "昨天 · 日式咖喱饭";

    {
        auto& g = m.menu.groups[0];
        g.name = "面食";
        g.candidateCount = 2;
        g.candidates[0].name = "番茄牛腩面";
        g.candidates[0].price = "¥28";
        g.candidates[0].score = 84;
        g.candidates[0].active = false;
        g.candidates[1].name = "葱油拌面";
        g.candidates[1].price = "¥12";
        g.candidates[1].score = 72;
        g.candidates[1].active = false;
    }
    {
        auto& g = m.menu.groups[1];
        g.name = "汤锅";
        g.candidateCount = 2;
        g.candidates[0].name = "砂锅豆腐汤";
        g.candidates[0].price = "¥22";
        g.candidates[0].score = 81;
        g.candidates[0].active = true;   // active = 推荐
        g.candidates[1].name = "韩式泡菜锅";
        g.candidates[1].price = "¥35";
        g.candidates[1].score = 78;
        g.candidates[1].active = false;
    }
    {
        auto& g = m.menu.groups[2];
        g.name = "小炒";
        g.candidateCount = 1;
        g.candidates[0].name = "麻辣香锅";
        g.candidates[0].price = "¥42";
        g.candidates[0].score = 87;
        g.candidates[0].active = false;
    }
    m.menu.groupCount = 3;
    m.menu.diceHint = "[A] 重抽 · [B] 记下";

    return m;
}

UiModel dn_build_ui_model();

} // namespace desknest

#endif // DESKNEST_UI_MODEL_H
