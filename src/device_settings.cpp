#include "device_settings.h"

#ifndef UNIT_TEST
#include <Preferences.h>
#endif

namespace desknest {

namespace {
constexpr const char* kNamespace = "dn_settings";
constexpr const char* kBlobKey = "device";

#ifdef UNIT_TEST
DeviceSettings s_test_store = dn_settings_defaults();
bool s_test_store_present = false;
#endif
} // namespace

bool dn_settings_load(DeviceSettings* out) {
    if (!out) return false;
#ifdef UNIT_TEST
    if (!s_test_store_present || !dn_settings_valid(s_test_store)) {
        *out = dn_settings_defaults();
        return false;
    }
    *out = s_test_store;
    return true;
#else
    Preferences prefs;
    DeviceSettings loaded = dn_settings_defaults();
    if (!prefs.begin(kNamespace, true)) {
        *out = loaded;
        return false;
    }
    const size_t size = prefs.getBytesLength(kBlobKey);
    const size_t read = size == sizeof(loaded)
        ? prefs.getBytes(kBlobKey, &loaded, sizeof(loaded))
        : 0;
    prefs.end();
    if (read != sizeof(loaded) || !dn_settings_valid(loaded)) {
        *out = dn_settings_defaults();
        return false;
    }
    *out = loaded;
    return true;
#endif
}

bool dn_settings_save(const DeviceSettings& settings) {
    if (!dn_settings_valid(settings)) return false;
#ifdef UNIT_TEST
    s_test_store = settings;
    s_test_store_present = true;
    return true;
#else
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    const size_t written = prefs.putBytes(kBlobKey, &settings, sizeof(settings));
    prefs.end();
    return written == sizeof(settings);
#endif
}

#ifdef UNIT_TEST
void dn_settings_test_reset_store() {
    s_test_store = dn_settings_defaults();
    s_test_store_present = false;
}

void dn_settings_test_seed_store(const DeviceSettings& settings) {
    s_test_store = settings;
    s_test_store_present = true;
}
#endif

} // namespace desknest
