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
#include "boot_splash.h"
#include "environment_module.h"
#include "ai_usage_module.h"
#include "focus_module.h"
#include "settings_module.h"
#include "what2eat_module.h"

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
    bool gestureConfirmEnabled = true;
    const char* gestureConfirmKey = "A";
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
    bool fiveHourAvailable = false;
    bool weeklyAvailable = false;
    uint8_t effectivePercent = 0;
    const char* statusText = "";
    const char* detailText = "";
    const char* fiveHourExpireAt = "";
    const char* weekExpireAt = "";
};

// Home is intentionally a small decision surface: one primary focus item,
// with the renderer free to place it in the larger (3:2) card.
enum HomeFocusKind : uint8_t {
    HOME_FOCUS_DEFAULT = 0,
    HOME_FOCUS_LIFE_REMINDER,
    HOME_FOCUS_AI_RISK,
};

struct UiHomeFocusProps {
    HomeFocusMode mode = HOME_FOCUS_AUTO;
    HomeFocusKind kind = HOME_FOCUS_DEFAULT;
    const char* title = "DeskNest";
    const char* detail = "今日状态已准备好";
    const char* actionLabel = "查看";
    uint8_t priority = 0;
    bool actionable = false;
};

struct UiCodexResetProps {
    const char* name = "";
    const char* expireAt = "";
};

struct UiAiUsageProps {
    uint8_t totalPercent = 0;
    uint8_t alertThresholdPct = 80;
    bool alertActive = false;
    UiUsageItemProps minimax;
    UiUsageItemProps codex;
    UiUsageItemProps chatgpt;
    UiCodexResetProps codexResets[4];
    uint8_t codexResetCount = 0;
    const char* updatedAtText = "";
    const char* warningText = "";
    const char* serverNow = "";
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

struct UiWhat2EatItemProps {
    char name[32] = {};
    uint16_t count = 0;
    char price[10] = {};
    uint8_t score = 0;
    bool selected = false;
};

struct UiWhat2EatProps {
    What2EatState state = WHAT2EAT_ABSENT;
    char recommendation[32] = {};
    UiWhat2EatItemProps items[WHAT2EAT_MAX_ITEMS];
    uint8_t itemCount = 0;
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

struct UiBootSplashProps {
    bool active = false;
    bool k10Ready = false;
    bool wifiReady = false;
    bool timeReady = false;
    bool aiReady = false;
    bool ready = false;
    bool failed = false;
    BootFailureReason failureReason = BOOT_FAIL_NONE;
    uint8_t progressPct = 0;
    uint8_t fadePct = 0;
};

struct UiBootFailureProps {
    const char* title = "";
    const char* detail = "";
    const char* hint = "";
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
    UiHomeFocusProps homeFocus;
    UiAiUsageProps aiUsage;
    UiWhat2EatProps what2eat;
    UiEnvironmentProps environment;
    UiSettingsProps settings;
    UiLandscapeOverviewProps landscapeOverview;
    UiFocusProps focus;
    UiCustomProps customPage;
    UiFaceDownProps faceDown;
    UiConfigProps config;
    UiBootSplashProps boot;
    UiBootFailureProps bootFailure;

    UiAnimationProps animation;
};

// Presentation-ready daily advice selection. The source strings stay in the
// render-data model; the renderer only displays the selected sentence.
inline const char* dn_home_daily_advice(const UiModel& m) {
    const uint32_t period = defaults::T_HOME_DAILY_ADVICE_ROTATE_MS;
    const bool use_secondary = period > 0 &&
        ((m.view.nowMs / period) % 2U) != 0U;
    const char* candidate = use_secondary
        ? m.overview.messageText
        : m.overview.suggestionText;
    if (!candidate || !candidate[0]) {
        candidate = use_secondary
            ? m.overview.suggestionText
            : m.overview.messageText;
    }
    return (candidate && candidate[0]) ? candidate : "保持专注";
}

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

    // Optional home-focus signal owned by the what2eat flow.
    bool what2eatChoicePending = false;

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
        case PAGE_PORTRAIT_WHAT2EAT:
        case PAGE_PORTRAIT_ENVIRONMENT:
        case PAGE_PORTRAIT_SETTINGS:    return SCREEN_PORTRAIT_DETAIL;
        case PAGE_LANDSCAPE_OVERVIEW:   return SCREEN_LANDSCAPE_OVERVIEW;
        case PAGE_LANDSCAPE_FOCUS:      return SCREEN_LANDSCAPE_FOCUS;
        case PAGE_LANDSCAPE_CUSTOM:     return SCREEN_LANDSCAPE_CUSTOM;
        case PAGE_SLEEP_FACE_DOWN:      return SCREEN_FACE_DOWN;
        case PAGE_CONFIG_PORTAL:        return SCREEN_CONFIG;
        case PAGE_BOOT_FAILURE:         return SCREEN_CONFIG;
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
    item.fiveHourAvailable = src.fiveHourAvailable;
    item.weeklyAvailable = src.weeklyAvailable;
    item.effectivePercent = src.effectivePercent;
    item.statusText = src.statusText;
    item.detailText = src.detailText;
    item.fiveHourExpireAt = src.fiveHourExpireAt;
    item.weekExpireAt = src.weekExpireAt;
    return item;
}

inline void dn_apply_what2eat_snapshot(UiModel& model,
                                       const What2EatSnapshot& snapshot) {
    model.what2eat = {};
    model.what2eat.state = snapshot.state;
    model.what2eat.itemCount = snapshot.itemCount > WHAT2EAT_MAX_ITEMS
        ? WHAT2EAT_MAX_ITEMS
        : snapshot.itemCount;

    const bool selection_valid = snapshot.selectedIndex >= 0
        && (uint8_t)snapshot.selectedIndex < model.what2eat.itemCount;
    if (selection_valid) {
        snprintf(model.what2eat.recommendation,
                 sizeof(model.what2eat.recommendation), "%s",
                 snapshot.items[(uint8_t)snapshot.selectedIndex].name);
    }

    for (uint8_t i = 0; i < model.what2eat.itemCount; ++i) {
        UiWhat2EatItemProps& out = model.what2eat.items[i];
        const What2EatItem& in = snapshot.items[i];
        snprintf(out.name, sizeof(out.name), "%s", in.name);
        out.count = in.count;
        snprintf(out.price, sizeof(out.price), "%s", in.price);
        out.score = in.score;
        out.selected = selection_valid && i == (uint8_t)snapshot.selectedIndex;
    }
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

inline UiHomeFocusProps dn_resolve_home_focus(const AIUsageStatus& ai,
                                              bool what2eatChoicePending,
                                              const DeviceSettings& settings = dn_settings_defaults()) {
    UiHomeFocusProps out;
    out.mode = static_cast<HomeFocusMode>(settings.homeFocusMode);
    if (settings.homeFocusMode == HOME_FOCUS_AI) {
        out.kind = HOME_FOCUS_AI_RISK;
        out.title = "AI 用量";
        out.detail = "查看当前额度和刷新时间";
        out.actionLabel = "查看 AI";
        out.priority = 90;
        out.actionable = true;
        return out;
    }
    if (settings.homeFocusMode == HOME_FOCUS_LIFE) {
        out.kind = HOME_FOCUS_LIFE_REMINDER;
        out.title = "what2eat";
        out.detail = "看看今天吃什么";
        out.actionLabel = "打开 what2eat";
        out.priority = 60;
        out.actionable = true;
        return out;
    }
    if (settings.homeFocusMode == HOME_FOCUS_MINIMAL) {
        out.kind = HOME_FOCUS_DEFAULT;
        out.title = "DeskNest";
        out.detail = "保持桌面清爽";
        out.actionLabel = "";
        out.priority = 0;
        out.actionable = false;
        return out;
    }
    // An explicit warning wins over the aggregate percentage: it may carry
    // source/cache/network context that a percentage alone cannot express.
    if (ai.warningText && ai.warningText[0] != '\0') {
        out.kind = HOME_FOCUS_AI_RISK;
        out.title = "AI 用量需要关注";
        out.detail = ai.warningText;
        out.actionLabel = "查看 AI";
        out.priority = 100;
        out.actionable = true;
    } else if (dn_settings_ai_alert_active(ai.totalPercent, settings)) {
        out.kind = HOME_FOCUS_AI_RISK;
        out.title = "AI 用量偏高";
        out.detail = "额度接近上限, 建议查看详情";
        out.actionLabel = "查看 AI";
        out.priority = 90;
        out.actionable = true;
    } else if (what2eatChoicePending) {
        out.kind = HOME_FOCUS_LIFE_REMINDER;
        out.title = "what2eat";
        out.detail = "今天还没有完成摇号";
        out.actionLabel = "打开 what2eat";
        out.priority = 60;
        out.actionable = true;
    }
    return out;
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
    m.aiUsage.serverNow = aiStatus.serverNow;
    m.aiUsage.nextRefreshInSec = aiStatus.nextRefreshInSec;
    m.overview.aiTotalPercent = aiStatus.totalPercent;

    m.aiUsage.alertThresholdPct = dn_settings_ai_threshold(s.settings);
    m.aiUsage.alertActive = dn_settings_ai_alert_active(aiStatus.totalPercent, s.settings);
    m.homeFocus = dn_resolve_home_focus(aiStatus, in.what2eatChoicePending, s.settings);

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

    const SettingsStatus settingsStatus = dn_settings_status(s.settings,
                                                             s.settingsSelectedIndex);
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

    m.bootFailure.title = "Network init failed";
    m.bootFailure.detail = "Retry required";
    m.bootFailure.hint = "Reboot to retry";

    m.animation.shakePhase = dn_shake_visual_phase(in.shakePhase);
    m.animation.shakeDirection = in.shakeDirection;
    m.animation.shakeProgressPct = shakeAnimationPercent(in.shakePhase);
    m.animation.pageChanged = false;
    m.animation.forceFullRedraw = false;

    return m;
}

UiModel dn_build_ui_model();

} // namespace desknest

#endif // DESKNEST_UI_MODEL_H
