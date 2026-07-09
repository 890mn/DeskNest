// src/ui_model.cpp
// 栖屏 DeskNest - transition adapter from current globals to UiModel

#include "ui_model.h"

#include "boot_splash.h"
#include "sensors.h"
#include "state_machine.h"

#include <Arduino.h>

namespace desknest {

static UiWiFiState dn_ui_wifi_state(AIWiFiState s) {
    switch (s) {
        case AI_WIFI_CONNECTED:    return UI_WIFI_CONNECTED;
        case AI_WIFI_CONNECTING:   return UI_WIFI_CONNECTING;
        case AI_WIFI_NO_SSID:      return UI_WIFI_NO_SSID;
        case AI_WIFI_AUTH_FAILED:  return UI_WIFI_AUTH_FAILED;
        case AI_WIFI_DISCONNECTED: return UI_WIFI_DISCONNECTED;
        case AI_WIFI_UNCONFIGURED:
        default:                   return UI_WIFI_UNCONFIGURED;
    }
}

static const char* dn_ui_clock_fallback_text(uint32_t now_ms) {
    static char s_clock[8];
    const uint32_t total_minutes = now_ms / 60000UL;
    const uint32_t hours = (total_minutes / 60UL) % 24UL;
    const uint32_t minutes = total_minutes % 60UL;
    snprintf(s_clock, sizeof(s_clock), "%02lu:%02lu",
             (unsigned long)hours, (unsigned long)minutes);
    return s_clock;
}

UiModel dn_build_ui_model() {
    UiModelInputs in = {};
    in.state = g_state.snapshot();
    in.nowMs = millis();

    const auto aht = g_sensors.aht20();
    in.temperatureValid = aht.valid;
    in.temperatureC = aht.temperatureC;
    in.humidityPct = aht.humidityPct;

    const auto lux = g_sensors.ltr303();
    in.luxValid = lux.valid;
    in.lux = lux.valid ? (uint16_t)lux.lux : 0;

    const auto bat = g_sensors.battery();
    in.batteryValid = bat.valid;
    in.batteryPercent = bat.percent;
    in.charging = bat.charging;

    in.shakePhase = g_gesture.shakePhase();
    in.shakeDirection = g_gesture.shakeDirection();

    UiModel model = dn_build_ui_model_from_inputs(in);
    const BootSplashStatus boot = dn_boot_splash_status();
    const bool boot_active = in.state.system == SYSTEM_BOOT || boot.active || in.state.page == PAGE_BOOT_FAILURE;

    const AIUsageStatus aiStatus = boot_active ? dn_ai_usage_demo_status() : dn_ai_usage_status();
    model.aiUsage.totalPercent = aiStatus.totalPercent;
    model.aiUsage.chatgpt = dn_usage_item(aiStatus.chatgpt);
    model.aiUsage.codex = dn_usage_item(aiStatus.codex);
    model.aiUsage.minimax = dn_usage_item(aiStatus.minimax);
    model.aiUsage.codexResetCount = aiStatus.codexResetCount;
    for (uint8_t i = 0; i < aiStatus.codexResetCount && i < 4; ++i) {
        model.aiUsage.codexResets[i].name = aiStatus.codexResets[i].name;
        model.aiUsage.codexResets[i].expireAt = aiStatus.codexResets[i].expireAt;
    }
    model.aiUsage.updatedAtText = aiStatus.updatedAtText;
    model.aiUsage.warningText = aiStatus.warningText;
    model.aiUsage.serverNow = aiStatus.serverNow;
    model.aiUsage.nextRefreshInSec = aiStatus.nextRefreshInSec;
    model.overview.aiTotalPercent = aiStatus.totalPercent;
    model.overview.aiStatusText = aiStatus.fromCache ? "cache" : "demo";
    model.overview.updatedAtText = aiStatus.updatedAtText;
    const char* net_time = boot_active ? "" : dn_ai_usage_time_text();
    model.overview.timeText = (net_time && net_time[0]) ? net_time : dn_ui_clock_fallback_text(in.nowMs);

    const AIWiFiStatus wifi = boot_active ? AIWiFiStatus{} : dn_ai_usage_wifi_status();
    model.status.wifiState = dn_ui_wifi_state(wifi.state);
    switch (model.status.wifiState) {
        case UI_WIFI_CONNECTED: {
            static char s_rssi[16];
            snprintf(s_rssi, sizeof(s_rssi), "RSSI %d", (int)wifi.rssi);
            model.status.wifiText = "WiFi OK";
            model.status.syncText = s_rssi;
            break;
        }
        case UI_WIFI_CONNECTING:
            model.status.wifiText = "WiFi Join";
            model.status.syncText = "Linking";
            break;
        case UI_WIFI_NO_SSID:
            model.status.wifiText = "WiFi No AP";
            model.status.syncText = wifi.ssidVisible ? "Weak AP" : "SSID miss";
            model.status.warning = true;
            break;
        case UI_WIFI_AUTH_FAILED:
            model.status.wifiText = "WiFi Auth";
            model.status.syncText = "Check pass";
            model.status.warning = true;
            break;
        case UI_WIFI_DISCONNECTED:
            model.status.wifiText = "WiFi Down";
            model.status.syncText = "Retrying";
            model.status.warning = true;
            break;
        case UI_WIFI_UNCONFIGURED:
        default:
            model.status.wifiText = "WiFi --";
            model.status.syncText = "No config";
            break;
    }

    model.boot.active = boot.active;
    model.boot.k10Ready = boot.k10Ready;
    model.boot.wifiReady = boot.wifiReady;
    model.boot.timeReady = boot.timeReady;
    model.boot.aiReady = boot.aiReady;
    model.boot.ready = boot.ready;
    model.boot.failed = boot.failed;
    model.boot.failureReason = boot.failureReason;
    model.boot.fadePct = boot.fadePct;

    switch (boot.failureReason) {
        case BOOT_FAIL_WIFI:
            model.bootFailure.title = "WiFi connect failed";
            model.bootFailure.detail = "Could not join saved network in 6s";
            model.bootFailure.hint = "Check SSID / signal, then reboot";
            break;
        case BOOT_FAIL_TIME:
            model.bootFailure.title = "Time sync failed";
            model.bootFailure.detail = "NTP did not complete in 6s";
            model.bootFailure.hint = "Check network access, then reboot";
            break;
        case BOOT_FAIL_AI:
            model.bootFailure.title = "AI fetch failed";
            model.bootFailure.detail = "Live usage data was not returned in 6s";
            model.bootFailure.hint = "Check server URL, then reboot";
            break;
        case BOOT_FAIL_NONE:
        default:
            break;
    }

    return model;
}

} // namespace desknest
