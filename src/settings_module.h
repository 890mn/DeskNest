// src/settings_module.h
// 栖屏 DeskNest - settings status module
//
// 当前阶段输出静态设置 rows；后续接 NVS 配置时在这里替换数据来源。

#ifndef DESKNEST_SETTINGS_MODULE_H
#define DESKNEST_SETTINGS_MODULE_H

#include <stdint.h>

namespace desknest {

struct SettingsRowStatus {
    const char* label = "";
    const char* value = "";
    bool selectable = false;
};

struct SettingsStatus {
    SettingsRowStatus rows[8];
    uint8_t rowCount = 0;
    uint8_t selectedIndex = 0;
    const char* dangerHint = "";
};

inline SettingsRowStatus dn_settings_row_status(const char* label,
                                                const char* value,
                                                bool selectable) {
    SettingsRowStatus row;
    row.label = label;
    row.value = value;
    row.selectable = selectable;
    return row;
}

inline uint8_t dn_settings_option_count(uint8_t row) {
    static const uint8_t counts[] = {4, 2, 2, 2};
    return row < 4 ? counts[row] : 1;
}

inline SettingsStatus dn_settings_status(const uint8_t values[4], uint8_t selectedIndex) {
    SettingsStatus status;
    static const char* const homeValues[] = {"自动", "AI优先", "生活优先", "极简"};
    static const char* const confirmValues[] = {"开启", "关闭"};
    static const char* const syncValues[] = {"省电", "实时"};
    static const char* const themeValues[] = {"深色", "柔和"};
    status.rowCount = 4;
    status.rows[0] = dn_settings_row_status("首页模块", homeValues[values[0] % 4], true);
    status.rows[1] = dn_settings_row_status("手势确认", confirmValues[values[1] % 2], true);
    status.rows[2] = dn_settings_row_status("同步模式", syncValues[values[2] % 2], true);
    status.rows[3] = dn_settings_row_status("界面主题", themeValues[values[3] % 2], true);
    status.selectedIndex = selectedIndex < status.rowCount ? selectedIndex : 0;
    status.dangerHint = "A 选择  B 切换";
    return status;
}

} // namespace desknest

#endif // DESKNEST_SETTINGS_MODULE_H
