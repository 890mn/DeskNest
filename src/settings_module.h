// src/settings_module.h
// 栖屏 DeskNest - settings status module
//
// Converts the owned DeviceSettings model into display rows.

#ifndef DESKNEST_SETTINGS_MODULE_H
#define DESKNEST_SETTINGS_MODULE_H

#include <stdint.h>
#include "device_settings.h"

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
    static const uint8_t counts[] = {4, 4, 3};
    return row < DN_SETTINGS_ROW_COUNT ? counts[row] : 1;
}

inline uint8_t dn_settings_value(const DeviceSettings& settings, uint8_t row) {
    switch (row) {
        case 0: return settings.homeFocusMode;
        case 1: return settings.aiAlertIndex;
        case 2: return settings.powerMode;
        default: return 0;
    }
}

inline void dn_settings_set_value(DeviceSettings& settings, uint8_t row, uint8_t value) {
    const uint8_t normalized = value % dn_settings_option_count(row);
    switch (row) {
        case 0: settings.homeFocusMode = normalized; break;
        case 1: settings.aiAlertIndex = normalized; break;
        case 2: settings.powerMode = normalized; break;
        default: break;
    }
}

inline SettingsStatus dn_settings_status(const DeviceSettings& settings,
                                         uint8_t selectedIndex) {
    SettingsStatus status;
    static const char* const homeValues[] = {"自动", "AI优先", "生活优先", "极简"};
    static const char* const alertValues[] = {"70%", "80%", "85%", "90%"};
    static const char* const powerValues[] = {"标准", "省电", "常亮"};
    status.rowCount = DN_SETTINGS_ROW_COUNT;
    status.rows[0] = dn_settings_row_status("首页焦点", homeValues[settings.homeFocusMode % 4], true);
    status.rows[1] = dn_settings_row_status("AI提醒", alertValues[settings.aiAlertIndex % 4], true);
    status.rows[2] = dn_settings_row_status("省电模式", powerValues[settings.powerMode % 3], true);
    status.selectedIndex = selectedIndex < status.rowCount ? selectedIndex : 0;
    status.dangerHint = "A 选择  B 切换";
    return status;
}

} // namespace desknest

#endif // DESKNEST_SETTINGS_MODULE_H
