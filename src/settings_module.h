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

inline SettingsStatus dn_settings_default_status() {
    SettingsStatus status;
    status.rowCount = 5;
    status.rows[0] = dn_settings_row_status("Power", "Balanced", true);
    status.rows[1] = dn_settings_row_status("Sync", "Battery", true);
    status.rows[2] = dn_settings_row_status("Density", "Normal", true);
    status.rows[3] = dn_settings_row_status("Rotate", "Auto", true);
    status.rows[4] = dn_settings_row_status("Theme", "Dark", true);
    status.selectedIndex = 0;
    status.dangerHint = "[A+B] Factory";
    return status;
}

} // namespace desknest

#endif // DESKNEST_SETTINGS_MODULE_H

