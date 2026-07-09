#ifndef DESKNEST_BOOT_SPLASH_H
#define DESKNEST_BOOT_SPLASH_H

#include <stdint.h>

namespace desknest {

enum BootFailureReason : uint8_t {
    BOOT_FAIL_NONE = 0,
    BOOT_FAIL_WIFI,
    BOOT_FAIL_TIME,
    BOOT_FAIL_AI,
};

struct BootSplashStatus {
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

void dn_boot_splash_begin(uint32_t now_ms);
void dn_boot_splash_update(uint32_t now_ms,
                           bool k10Ready,
                           bool wifiReady,
                           bool timeReady,
                           bool aiReady,
                           bool failed = false,
                           BootFailureReason failureReason = BOOT_FAIL_NONE);
BootSplashStatus dn_boot_splash_status();
bool dn_boot_splash_active();

} // namespace desknest

#endif // DESKNEST_BOOT_SPLASH_H
