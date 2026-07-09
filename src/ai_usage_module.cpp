// src/ai_usage_module.cpp

#include "ai_usage_module.h"
#include "config.h"

#ifndef UNIT_TEST

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>

namespace desknest {
namespace {

constexpr const char* kUsagePaths[] = {
    "/cc-switch/status.json",
    "/status.json",
};
constexpr uint32_t kRefreshIntervalMs = 30000;

static AIUsageParseStorage s_storage;
static AIUsageStatus s_cached;
static AIWiFiStatus s_wifi_status;
static bool s_loaded_once = false;
static uint32_t s_last_load_ms = 0;
static uint32_t s_last_wifi_attempt_ms = 0;
static uint32_t s_last_wifi_scan_ms = 0;
static bool s_wifi_started = false;
static bool s_time_sync_started = false;
static bool s_time_synced = false;
static bool s_data_ready = false;
static bool s_live_data_ready = false;
static uint32_t s_last_time_sync_ms = 0;
static uint32_t s_time_base_ms = 0;
static time_t s_time_base_epoch = 0;

constexpr uint32_t kWiFiConnectCooldownMs = 20000;
constexpr uint32_t kWiFiNoSsidCooldownMs = 45000;
constexpr uint32_t kWiFiScanCooldownMs = 30000;
constexpr uint32_t kTimeSyncCooldownMs = 60000;
constexpr uint32_t kTimeSyncPendingRetryMs = 2500;
constexpr uint32_t kBootstrapRetryMs = 2000;

bool tokennest_configured() {
    return DESKNEST_TOKENNEST_STATUS_URL[0] != '\0';
}

bool wifi_configured() {
    return DESKNEST_WIFI_SSID[0] != '\0';
}

uint16_t seconds_until(uint32_t now, uint32_t last_ms, uint32_t cooldown_ms) {
    if (last_ms == 0 || now - last_ms >= cooldown_ms) return 0;
    return (uint16_t)((cooldown_ms - (now - last_ms) + 999) / 1000);
}

bool should_retry_connect(uint32_t now, wl_status_t st) {
    const uint32_t cooldown = (st == WL_NO_SSID_AVAIL) ? kWiFiNoSsidCooldownMs : kWiFiConnectCooldownMs;
    return s_last_wifi_attempt_ms == 0 || now - s_last_wifi_attempt_ms >= cooldown;
}

void ensure_wifi_station_started() {
    if (s_wifi_started) return;
    s_wifi_started = true;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(true);
    Serial.println("[D][AI][WIFI] init: STA + autoReconnect + modem sleep");
}

void refresh_wifi_scan(uint32_t now) {
    if (s_last_wifi_scan_ms != 0 && now - s_last_wifi_scan_ms < kWiFiScanCooldownMs) return;
    s_last_wifi_scan_ms = now;

    const int found = WiFi.scanNetworks(false, false, false, 110, 0, DESKNEST_WIFI_SSID);
    s_wifi_status.ssidVisible = (found > 0);
    s_wifi_status.rssi = (found > 0) ? (int8_t)WiFi.RSSI(0) : 0;
    Serial.printf("[D][AI][WIFI] scan ssid='%s' found=%d rssi=%d\n",
                  DESKNEST_WIFI_SSID, found, (int)s_wifi_status.rssi);
    WiFi.scanDelete();
}

void apply_server_time_basis(const char* server_now_text) {
    const time_t epoch = dn_apply_server_now_boot_offset(dn_parse_iso8601_epoch(server_now_text));
    if (epoch <= 0) return;
    s_time_synced = true;
    s_time_base_epoch = epoch;
    s_time_base_ms = millis();

    struct tm tm_now;
    localtime_r(&epoch, &tm_now);
    Serial.printf("[D][AI][TIME] serverNow %04d-%02d-%02d %02d:%02d:%02d\n",
                  tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                  tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

void ensure_network_time(uint32_t now) {
    if (WiFi.status() != WL_CONNECTED) return;
    if (s_time_synced && now - s_last_time_sync_ms < kTimeSyncCooldownMs) return;
    if (!s_time_synced && s_last_time_sync_ms != 0 && now - s_last_time_sync_ms < kTimeSyncPendingRetryMs) return;

    s_last_time_sync_ms = now;
    if (!s_time_sync_started) {
        s_time_sync_started = true;
        configTzTime("CST-8", "ntp.aliyun.com", "ntp.ntsc.ac.cn", "pool.ntp.org");
        Serial.println("[D][AI][TIME] ntp init");
    }

    struct tm tm_now;
    if (!getLocalTime(&tm_now, 200)) {
        Serial.println("[D][AI][TIME] sync pending");
        return;
    }

    time_t epoch = mktime(&tm_now);
    if (epoch <= 0) {
        Serial.println("[D][AI][TIME] invalid epoch");
        return;
    }

    s_time_synced = true;
    s_time_base_epoch = epoch;
    s_time_base_ms = millis();
    Serial.printf("[D][AI][TIME] synced %04d-%02d-%02d %02d:%02d:%02d\n",
                  tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                  tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

bool refresh_wifi_link(uint32_t now, bool request_connect) {
    s_wifi_status = {};
    s_wifi_status.configured = wifi_configured();
    if (!s_wifi_status.configured) {
        s_wifi_status.state = AI_WIFI_UNCONFIGURED;
        return false;
    }

    ensure_wifi_station_started();
    wl_status_t st = WiFi.status();
    s_wifi_status.rawStatus = (uint8_t)st;

    if (st == WL_CONNECTED) {
        s_wifi_status.state = AI_WIFI_CONNECTED;
        s_wifi_status.ssidVisible = true;
        s_wifi_status.rssi = (int8_t)WiFi.RSSI();
        s_wifi_status.retryInSec = 0;
        ensure_network_time(now);
        return true;
    }

    if (st == WL_NO_SSID_AVAIL) {
        s_wifi_status.state = AI_WIFI_NO_SSID;
        refresh_wifi_scan(now);
    } else if (st == WL_CONNECT_FAILED) {
        s_wifi_status.state = AI_WIFI_AUTH_FAILED;
    } else if (st == WL_IDLE_STATUS) {
        s_wifi_status.state = AI_WIFI_CONNECTING;
    } else {
        s_wifi_status.state = AI_WIFI_DISCONNECTED;
    }

    const uint32_t cooldown = (st == WL_NO_SSID_AVAIL) ? kWiFiNoSsidCooldownMs : kWiFiConnectCooldownMs;
    s_wifi_status.retryInSec = seconds_until(now, s_last_wifi_attempt_ms, cooldown);

    if (!request_connect || !should_retry_connect(now, st)) {
        return false;
    }

    s_last_wifi_attempt_ms = now;
    s_wifi_status.retryInSec = seconds_until(now, s_last_wifi_attempt_ms, cooldown);
    Serial.printf("[D][AI][WIFI] connect ssid='%s' status=%d\n", DESKNEST_WIFI_SSID, (int)st);
    WiFi.disconnect(false, false);
    WiFi.begin(DESKNEST_WIFI_SSID, DESKNEST_WIFI_PASS);

    const uint32_t deadline = millis() + 1500;
    wl_status_t cur = WiFi.status();
    while (cur == WL_IDLE_STATUS && millis() < deadline) {
        delay(50);
        cur = WiFi.status();
    }

    s_wifi_status.rawStatus = (uint8_t)cur;
    if (cur == WL_CONNECTED) {
        s_wifi_status.state = AI_WIFI_CONNECTED;
        s_wifi_status.ssidVisible = true;
        s_wifi_status.rssi = (int8_t)WiFi.RSSI();
        s_wifi_status.retryInSec = 0;
        ensure_network_time(now);
        Serial.printf("[D][AI][WIFI] connected ip=%s rssi=%d\n",
                      WiFi.localIP().toString().c_str(), (int)s_wifi_status.rssi);
        return true;
    }

    if (cur == WL_NO_SSID_AVAIL) {
        s_wifi_status.state = AI_WIFI_NO_SSID;
        refresh_wifi_scan(now);
    } else if (cur == WL_CONNECT_FAILED) {
        s_wifi_status.state = AI_WIFI_AUTH_FAILED;
    } else {
        s_wifi_status.state = AI_WIFI_CONNECTING;
    }
    s_wifi_status.retryInSec = seconds_until(now, s_last_wifi_attempt_ms, cooldown);
    Serial.printf("[D][AI][WIFI] pending/fail status=%d retry=%us visible=%d\n",
                  (int)cur, (unsigned)s_wifi_status.retryInSec, s_wifi_status.ssidVisible ? 1 : 0);
    return false;
}

bool ensure_wifi(uint32_t now) {
    return refresh_wifi_link(now, true);
}

bool fetch_tokennest_status(AIUsageStatus* out, uint32_t now) {
    if (!out) return false;
    if (!tokennest_configured()) {
        Serial.println("[D][AI][HTTP] skip: TokenNest URL not configured");
        return false;
    }
    if (!ensure_wifi(now)) return false;

    static char json[1536];
    HTTPClient http;
    http.setTimeout(2500);
    Serial.printf("[D][AI][HTTP] GET %s\n", DESKNEST_TOKENNEST_STATUS_URL);
    if (!http.begin(DESKNEST_TOKENNEST_STATUS_URL)) {
        Serial.println("[D][AI][HTTP] begin failed");
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[D][AI][HTTP] status=%d\n", code);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    const int n = stream->readBytes(json, sizeof(json) - 1);
    json[n] = '\0';
    http.end();

    if (n <= 0) {
        Serial.println("[D][AI][HTTP] empty body");
        return false;
    }
    const bool parsed = dn_ai_usage_parse_cc_switch_status(json, &s_storage, out);
    if (parsed) {
        out->fromCache = false;
        apply_server_time_basis(out->serverNow);
    }
    Serial.printf("[D][AI][HTTP] bytes=%d parsed=%d total=%u next=%us warn='%s'\n",
                  n, parsed ? 1 : 0, parsed ? (unsigned)out->totalPercent : 0,
                  parsed ? (unsigned)out->nextRefreshInSec : 0,
                  parsed ? out->warningText : "");
    return parsed;
}

} // namespace

AIUsageStatus dn_ai_usage_status() {
    const uint32_t now = millis();
    refresh_wifi_link(now, true);
    const uint32_t refresh_ms = s_data_ready ? kRefreshIntervalMs : kBootstrapRetryMs;
    if (!s_loaded_once || now - s_last_load_ms >= refresh_ms) {
        AIUsageStatus parsed;
        Serial.printf("[D][AI] refresh start loaded=%d age=%lums\n",
                      s_loaded_once ? 1 : 0,
                      (unsigned long)(s_loaded_once ? now - s_last_load_ms : 0));
        if (fetch_tokennest_status(&parsed, now)) {
            s_cached = parsed;
            s_data_ready = true;
            s_live_data_ready = true;
            Serial.println("[D][AI] source=http");
        } else if (!s_loaded_once) {
            s_cached = dn_ai_usage_demo_status();
            s_data_ready = false;
            s_live_data_ready = false;
            Serial.println("[D][AI] source=demo");
        } else {
            Serial.println("[D][AI] source=previous");
        }
        s_loaded_once = true;
        s_last_load_ms = now;
    }
    AIUsageStatus current = s_cached;
    const uint32_t elapsed = now >= s_last_load_ms ? now - s_last_load_ms : 0;
    current.nextRefreshInSec = elapsed >= refresh_ms
        ? 0
        : (uint16_t)((refresh_ms - elapsed + 999) / 1000);
    return current;
}

AIWiFiStatus dn_ai_usage_wifi_status() {
    refresh_wifi_link(millis(), true);
    return s_wifi_status;
}

void dn_ai_usage_service_begin() {
    const uint32_t now = millis();
    ensure_wifi_station_started();
    refresh_wifi_link(now, true);
}

void dn_ai_usage_service_tick() {
    refresh_wifi_link(millis(), true);
}

const char* dn_ai_usage_time_text() {
    static char s_time_text[8] = "";
    if (!s_time_synced || s_time_base_epoch <= 0) return "";

    const uint32_t now_ms = millis();
    const time_t epoch = s_time_base_epoch + (time_t)((now_ms - s_time_base_ms) / 1000UL);
    struct tm tm_now;
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || !defined(UNIT_TEST)
    localtime_r(&epoch, &tm_now);
#else
    tm_now = *localtime(&epoch);
#endif
    snprintf(s_time_text, sizeof(s_time_text), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    return s_time_text;
}

time_t dn_ai_usage_now_epoch() {
    if (!s_time_synced || s_time_base_epoch <= 0) return 0;
    const uint32_t now_ms = millis();
    return s_time_base_epoch + (time_t)((now_ms - s_time_base_ms) / 1000UL);
}

bool dn_ai_usage_time_ready() {
    return s_time_synced && s_time_base_epoch > 0;
}

bool dn_ai_usage_data_ready() {
    return s_data_ready;
}

bool dn_ai_usage_live_data_ready() {
    return s_live_data_ready;
}

} // namespace desknest

#else

namespace desknest {
AIUsageStatus dn_ai_usage_status() {
    return dn_ai_usage_demo_status();
}
AIWiFiStatus dn_ai_usage_wifi_status() {
    return {};
}
void dn_ai_usage_service_begin() {
}
void dn_ai_usage_service_tick() {
}
const char* dn_ai_usage_time_text() {
    return "";
}
time_t dn_ai_usage_now_epoch() {
    return 0;
}
bool dn_ai_usage_time_ready() {
    return false;
}
bool dn_ai_usage_data_ready() {
    return false;
}
bool dn_ai_usage_live_data_ready() {
    return false;
}
} // namespace desknest

#endif
