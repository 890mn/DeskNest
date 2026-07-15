// Persistent, board-owned DeskNest settings and pure policy helpers.

#ifndef DESKNEST_DEVICE_SETTINGS_H
#define DESKNEST_DEVICE_SETTINGS_H

#include <stdint.h>

namespace desknest {

constexpr uint8_t DN_SETTINGS_SCHEMA_VERSION = 1;
constexpr uint8_t DN_SETTINGS_ROW_COUNT = 3;

enum HomeFocusMode : uint8_t {
    HOME_FOCUS_AUTO = 0,
    HOME_FOCUS_AI,
    HOME_FOCUS_LIFE,
    HOME_FOCUS_MINIMAL,
};

enum PowerMode : uint8_t {
    POWER_STANDARD = 0,
    POWER_SAVER,
    POWER_ALWAYS_ON,
};

struct DeviceSettings {
    uint8_t schemaVersion = DN_SETTINGS_SCHEMA_VERSION;
    uint8_t homeFocusMode = HOME_FOCUS_AUTO;
    uint8_t aiAlertIndex = 1;  // 80%, preserving the former fixed trigger.
    uint8_t powerMode = POWER_STANDARD;
};

struct PowerTimeouts {
    uint32_t ambientMs;
    uint32_t sleepMs;
    bool idleSleepEnabled;

    PowerTimeouts(uint32_t ambient = 30000,
                  uint32_t sleep = 90000,
                  bool enabled = true)
        : ambientMs(ambient), sleepMs(sleep), idleSleepEnabled(enabled) {}
};

inline DeviceSettings dn_settings_defaults() {
    return DeviceSettings{};
}

inline bool dn_settings_valid(const DeviceSettings& settings) {
    return settings.schemaVersion == DN_SETTINGS_SCHEMA_VERSION &&
           settings.homeFocusMode <= HOME_FOCUS_MINIMAL &&
           settings.aiAlertIndex < 4 &&
           settings.powerMode <= POWER_ALWAYS_ON;
}

inline uint8_t dn_settings_ai_threshold(const DeviceSettings& settings) {
    static const uint8_t thresholds[] = {70, 80, 85, 90};
    return thresholds[settings.aiAlertIndex < 4 ? settings.aiAlertIndex : 1];
}

inline bool dn_settings_ai_alert_active(uint8_t percent,
                                        const DeviceSettings& settings) {
    return percent >= dn_settings_ai_threshold(settings);
}

inline PowerTimeouts dn_settings_power_timeouts(const DeviceSettings& settings) {
    switch (settings.powerMode) {
        case POWER_SAVER:    return {15000, 45000, true};
        case POWER_ALWAYS_ON:return {0, 0, false};
        case POWER_STANDARD:
        default:             return {30000, 90000, true};
    }
}

bool dn_settings_load(DeviceSettings* out);
bool dn_settings_save(const DeviceSettings& settings);

#ifdef UNIT_TEST
void dn_settings_test_reset_store();
void dn_settings_test_seed_store(const DeviceSettings& settings);
#endif

} // namespace desknest

#endif // DESKNEST_DEVICE_SETTINGS_H
