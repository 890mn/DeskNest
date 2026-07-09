#include "boot_splash.h"

namespace desknest {
namespace {

constexpr uint32_t kBootHoldMs = 3000;
static uint32_t s_boot_fade_ms = 0;
static uint32_t s_fade_started_at_ms = 0;
static bool s_fade_started = false;

static BootSplashStatus s_status;
static uint32_t s_started_at_ms = 0;
static bool s_started = false;

} // namespace

void dn_boot_splash_begin(uint32_t now_ms) {
    s_started = true;
    s_started_at_ms = now_ms;
    s_fade_started_at_ms = 0;
    s_fade_started = false;
    s_status = {};
    s_status.active = true;
}

void dn_boot_splash_update(uint32_t now_ms,
                           bool k10Ready,
                           bool wifiReady,
                           bool timeReady,
                           bool aiReady,
                           bool failed,
                           BootFailureReason failureReason) {
    if (!s_started) dn_boot_splash_begin(now_ms);

    s_status.k10Ready = k10Ready;
    s_status.wifiReady = wifiReady;
    s_status.timeReady = timeReady;
    s_status.aiReady = aiReady;
    s_status.failed = failed;
    s_status.failureReason = failureReason;

    const uint32_t elapsed = now_ms >= s_started_at_ms ? (now_ms - s_started_at_ms) : 0;
    const bool all_ready = k10Ready && wifiReady && timeReady && aiReady;
    s_status.ready = all_ready;

    if (failed) {
        s_status.active = false;
        s_status.fadePct = 100;
        return;
    }

    if (!s_fade_started && all_ready && elapsed >= kBootHoldMs) {
        s_fade_started = true;
        s_fade_started_at_ms = now_ms;
    }

    if (!s_fade_started) {
        s_status.active = true;
        s_status.fadePct = 0;
        return;
    }

    if (s_boot_fade_ms == 0U) {
        s_status.active = false;
        s_status.fadePct = 100;
        return;
    }

    const uint32_t fade_elapsed = now_ms >= s_fade_started_at_ms ? (now_ms - s_fade_started_at_ms) : 0;
    if (fade_elapsed >= s_boot_fade_ms) {
        s_status.active = false;
        s_status.fadePct = 100;
        return;
    }

    s_status.active = true;
    s_status.fadePct = (uint8_t)((fade_elapsed * 100U) / s_boot_fade_ms);
    if (s_status.fadePct > 100) s_status.fadePct = 100;
}

BootSplashStatus dn_boot_splash_status() {
    return s_status;
}

bool dn_boot_splash_active() {
    return s_status.active;
}

} // namespace desknest
