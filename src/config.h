// src/config.h
// 栖屏 DeskNest — 4 轴状态枚举与编译期配置
// "栖于桌面，息于常亮之间"
//
// 此文件是整个项目的"宪法"：所有状态枚举、阈值、命名空间都在这里集中定义。
// 修改前请先读 README 与 docs/architecture.md。

#ifndef DESKNEST_CONFIG_H
#define DESKNEST_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// 命名空间与前缀
// ============================================================================

namespace desknest {

// 所有公开函数/类加 dn_ 前缀
// 所有 NVS key 加 dn_ 前缀
// 所有日志以 [D] 开头

// ============================================================================
// 版本
// ============================================================================

#ifndef DESKNEST_VERSION
#define DESKNEST_VERSION "1.0"
#endif

// Optional TokenNest HTTP bridge.
// These should be set in platformio.local.ini (gitignored), NOT the public
// platformio.ini. If left empty here, the firmware falls back to NVS-config
// (Config Portal flow — see nvs_key::WIFI_SSID / nvs_key::WIFI_PASS /

#ifndef DESKNEST_WIFI_SSID
#define DESKNEST_WIFI_SSID ""
#endif

#ifndef DESKNEST_WIFI_PASS
#define DESKNEST_WIFI_PASS ""
#endif

#ifndef DESKNEST_TOKENNEST_STATUS_URL
#define DESKNEST_TOKENNEST_STATUS_URL ""
#endif

// ============================================================================
// 4 轴状态枚举（核心 —— 见 plan §2）
// ============================================================================

// 1) 系统运行状态
enum SystemState : uint8_t {
    SYSTEM_BOOT = 0,              // 启动中
    SYSTEM_IDLE,                  // 新增：每帧处理前都是这个起点（每路输入独立从 IDLE 出发）
    SYSTEM_ACTIVE,                // 正常运行
    SYSTEM_AMBIENT,               // 30s 无操作，背光 30%
    SYSTEM_LIGHT_SLEEP,           // 90s 无操作，light-sleep
    SYSTEM_FACE_DOWN_SLEEP,       // 翻面栖息（核心差异化）
    SYSTEM_CONFIG,                // 配网页面
};

// 1.5) 翻面子状态（显式区分 face_up / face_down，独立于 system 状态）
enum FaceSubState : uint8_t {
    FACE_STATE_UP   = 0,          // 设备正面朝上（默认）—— 可以处理朝向/页面
    FACE_STATE_DOWN = 1,          // 设备翻面朝下 —— roost 模式，屏蔽大部分输入
};

// 2) 设备姿态
enum OrientationState : uint8_t {
    ORIENTATION_UNKNOWN = 0,
    ORIENTATION_PORTRAIT,
    ORIENTATION_LANDSCAPE,
    ORIENTATION_FACE_DOWN,
};

// 3) UI 页面（9 个：竖 5 + 横 3 + 特殊 1）
enum UIPage : uint8_t {
    PAGE_PORTRAIT_OVERVIEW = 0,
    PAGE_PORTRAIT_AI_USAGE,
    PAGE_PORTRAIT_WHAT2EAT,
    PAGE_PORTRAIT_ENVIRONMENT,
    PAGE_PORTRAIT_SETTINGS,
    PAGE_LANDSCAPE_OVERVIEW,
    PAGE_LANDSCAPE_FOCUS,
    PAGE_LANDSCAPE_CUSTOM,
    PAGE_SLEEP_FACE_DOWN,
    PAGE_CONFIG_PORTAL,
    PAGE_BOOT_FAILURE,
    PAGE_COUNT,  // 哨兵
};

// 4) 手势事件
enum GestureEvent : uint8_t {
    GESTURE_NONE = 0,
    GESTURE_SHAKE_LEFT,
    GESTURE_SHAKE_RIGHT,
    GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE,
    GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT,
    GESTURE_FACE_DOWN,
    GESTURE_FACE_UP_OPEN,
    GESTURE_TAP,
};

// 5) 按键事件
enum ButtonEvent : uint8_t {
    BUTTON_NONE = 0,
    BUTTON_NEXT,        // A 短按
    BUTTON_PREV,        // B 短按
    BUTTON_SELECT,      // A+B 同时
    BUTTON_BACK,        // B 长按 1s
    BUTTON_MENU,        // A 长按 1s
    BUTTON_FACTORY,     // A+B 长按 3s
};

// ============================================================================
// 用户可配置项（见 plan §9.5、§9.6、§3.7、§4.5、§4.6）
// ============================================================================

// 电源档位预设
enum PowerProfile : uint8_t {
    POWER_PROFILE_BALANCED = 0,  // 默认：30s/90s
    POWER_PROFILE_AGGRESSIVE,    // 15s/45s
    POWER_PROFILE_LAZY,          // 60s/180s
};

// 同步策略
enum SyncProfile : uint8_t {
    SYNC_BATTERY_SAVER = 0,      // 默认高续航
    SYNC_REALTIME,               // 高时效
};

// 信息密度
enum InfoDensity : uint8_t {
    DENSITY_COMPACT = 0,
    DENSITY_NORMAL,              // 默认
};

// 横屏用量样式
enum LandscapeUsageStyle : uint8_t {
    LAND_USAGE_BAR = 0,          // 默认数字条
    LAND_USAGE_RING,             // 圆环
};

// 导航偏好
enum NavPreference : uint8_t {
    NAV_BUTTON_FIRST = 0,        // 默认
    NAV_GESTURE_FIRST,
    NAV_BOTH,
};
enum GestureConfirmButton : uint8_t { GESTURE_CONFIRM_NONE = 0, GESTURE_CONFIRM_A, GESTURE_CONFIRM_B };
#ifndef DESKNEST_GESTURE_CONFIRM_BUTTON
#define DESKNEST_GESTURE_CONFIRM_BUTTON GESTURE_CONFIRM_A
#endif

// Navigation takeover mode used while validating physical gestures.  The
// gesture-first default keeps the two short-press buttons available for later
// confirmation/shortcut assignments; long-press actions remain unaffected.
#ifndef DESKNEST_NAV_PREFERENCE
#define DESKNEST_NAV_PREFERENCE NAV_GESTURE_FIRST
#endif

// 旋转锁定
enum RotationLock : uint8_t {
    ROT_AUTO = 0,                // 默认自动
    ROT_LOCKED_PORTRAIT,
    ROT_LOCKED_LANDSCAPE,
    ROT_LOCKED_TEMP_5S,          // 临时解锁 5s
};

// 主题
enum Theme : uint8_t {
    THEME_DARK = 0,
    THEME_LIGHT,
    THEME_AUTO,                  // 跟环境光
};

// 横屏默认页
enum LandscapeDefaultPage : uint8_t {
    LAND_DEFAULT_OVERVIEW = 0,
    LAND_DEFAULT_FOCUS,
    LAND_DEFAULT_CUSTOM,
};

// ============================================================================
// 编译期默认阈值（见 plan §9.5、§9.6）
// ============================================================================

namespace defaults {
    // 电源档位
    constexpr uint32_t T_AMBIENT_MS_BALANCED    = 30 * 1000;
    constexpr uint32_t T_SLEEP_MS_BALANCED      = 90 * 1000;
    constexpr uint32_t T_AMBIENT_MS_AGGRESSIVE  = 15 * 1000;
    constexpr uint32_t T_SLEEP_MS_AGGRESSIVE    = 45 * 1000;
    constexpr uint32_t T_AMBIENT_MS_LAZY        = 60 * 1000;
    constexpr uint32_t T_SLEEP_MS_LAZY          = 180 * 1000;

    // 同步周期（高续航 / 高时效 × ACTIVE / AMBIENT / FACE_DOWN）
    constexpr uint32_t SYNC_BS_ACTIVE_MS        = 22 * 60 * 1000;  // 22 min
    constexpr uint32_t SYNC_BS_AMBIENT_MS       = 60 * 60 * 1000;  // 60 min
    constexpr uint32_t SYNC_RT_ACTIVE_MS        = 5 * 60 * 1000;   // 5 min

    // K10 板载 AHT20 温度读数校准：统一在传感器边界补偿，默认下调 6°C。
    constexpr float    AHT20_TEMPERATURE_OFFSET_C = -6.0f;

    // 首页“今日建议”在已有模型文案之间轮换的周期。
    constexpr uint32_t T_HOME_DAILY_ADVICE_ROTATE_MS = 10 * 1000;

    // 翻面 / 旋转 / 摇动滞回
    constexpr uint16_t T_ROTATE_STABLE_MS       = 400;
    constexpr uint16_t T_FACE_DOWN_STABLE_MS    = 800;
    constexpr uint16_t T_FACE_UP_STABLE_MS      = 300;
    constexpr uint16_t T_SHAKE_COOLDOWN_MS      = 450;
    constexpr float    G_ROTATE_THRESHOLD       = 0.7f;
    // K10 实测：设备正面朝桌放（翻面）时 Z ≈ +0.92g。
    // +0.7 进入 face-down，低于 +0.6 则认为已经离开；中间 0.1g 是滞回带。
    // face_up 字段名为兼容现有串口 REPL 保留，语义是 face-down release。
    constexpr float    G_FACE_DOWN_THRESHOLD    =  0.7f;
    constexpr float    G_FACE_UP_THRESHOLD      =  0.6f;
      constexpr float    G_SHAKE_THRESHOLD        = 0.30f;

    // 告警阈值
    constexpr uint8_t  ALERT_ORANGE_PCT         = 50;
    constexpr uint8_t  ALERT_RED_PCT            = 85;

    // 屏幕
    constexpr uint8_t  SCREEN_BRIGHT_ACTIVE     = 100;
    constexpr uint8_t  SCREEN_BRIGHT_AMBIENT    = 30;
    constexpr uint16_t SCREEN_W                 = 240;
    constexpr uint16_t SCREEN_H                 = 320;

    // RGB 灯
    constexpr uint8_t  RGB_COUNT                = 3;
} // namespace defaults

// ============================================================================
// NVS namespace 与 key 集中表
// ============================================================================

namespace nvs_ns {
    constexpr const char* WIFI        = "dn_wifi";
    constexpr const char* STATUS      = "dn_status";
    constexpr const char* STATS       = "dn_stats";
    constexpr const char* POWER       = "dn_power";
    constexpr const char* SYNC        = "dn_sync";
    constexpr const char* UI          = "dn_ui";
    constexpr const char* NAV         = "dn_nav";
    constexpr const char* ROT         = "dn_rot";
    constexpr const char* THEME       = "dn_theme";
} // namespace nvs_ns

namespace nvs_key {
    constexpr const char* WIFI_SSID          = "ssid";
    constexpr const char* WIFI_PASS          = "pass";
    constexpr const char* CC_SWITCH_URL      = "cc_url";
    constexpr const char* CC_SWITCH_TOKEN    = "cc_tok";

    constexpr const char* POWER_PROFILE      = "profile";
    constexpr const char* T_AMBIENT_MS       = "t_amb";
    constexpr const char* T_SLEEP_MS         = "t_slp";

    constexpr const char* SYNC_PROFILE       = "profile";
    constexpr const char* DENSITY            = "density";
    constexpr const char* LAND_USAGE_STYLE   = "l_usage";
    constexpr const char* LAND_DEFAULT_PAGE  = "l_page";
    constexpr const char* NAV_PREF           = "nav";
    constexpr const char* ROT_LOCK           = "rot";
    constexpr const char* THEME              = "theme";

    // 统计
    constexpr const char* BOOT_COUNT         = "boot";
    constexpr const char* WIFI_SUCC          = "w_succ";
    constexpr const char* WIFI_FAIL          = "w_fail";
    constexpr const char* SYNC_OK            = "s_ok";
    constexpr const char* SYNC_FAIL          = "s_fail";
    constexpr const char* SLEEP_CYCLES       = "sleep";
    constexpr const char* BATTERY_LOW        = "bat_lo";
    constexpr const char* LAST_RESET_REASON  = "rst_rsn";
} // namespace nvs_key

} // namespace desknest

#endif // DESKNEST_CONFIG_H
