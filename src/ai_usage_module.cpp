// src/ai_usage_module.cpp

#include "ai_usage_module.h"
#include "config.h"

#ifndef UNIT_TEST

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace desknest {
namespace {

constexpr const char* kUsagePaths[] = {
    "/cc-switch/status.json",
    "/status.json",
};
constexpr uint32_t kRefreshIntervalMs = 30000;

static AIUsageParseStorage s_parse_storage[2];
static uint8_t s_active_parse_storage = 0;
static AIUsageStatus s_cached;
static AIWiFiStatus s_wifi_status;
static AIWiFiStatus s_wifi_working;
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
static SemaphoreHandle_t s_state_mutex = nullptr;

struct PublishedTimeState {
    bool ready = false;
    uint32_t baseMs = 0;
    time_t baseEpoch = 0;
};

static PublishedTimeState s_published_time;

void ensure_state_mutex() {
    if (!s_state_mutex) s_state_mutex = xSemaphoreCreateMutex();
}

struct StateLock {
    explicit StateLock(TickType_t wait_ticks = portMAX_DELAY) {
        ensure_state_mutex();
        locked_ = s_state_mutex && xSemaphoreTake(s_state_mutex, wait_ticks) == pdTRUE;
    }
    ~StateLock() { if (locked_) xSemaphoreGive(s_state_mutex); }
    bool locked() const { return locked_; }

private:
    bool locked_ = false;
};

constexpr TickType_t kSnapshotLockWaitTicks = pdMS_TO_TICKS(2);

void bind_parsed_storage(AIUsageStatus* status, AIUsageParseStorage* storage) {
    if (!status || !storage) return;
    status->updatedAtText = storage->updatedAtText[0] ? storage->updatedAtText : "cached";
    status->warningText = storage->warningText;
    status->serverNow = storage->serverNow;
    status->chatgpt.statusText = storage->chatgptStatus;
    status->chatgpt.detailText = storage->chatgptDetail;
    status->chatgpt.fiveHourExpireAt = storage->chatgptFiveHourExpireAt;
    status->chatgpt.weekExpireAt = storage->chatgptWeekExpireAt;
    status->minimax.statusText = storage->minimaxStatus;
    status->minimax.detailText = storage->minimaxDetail;
    status->minimax.fiveHourExpireAt = storage->minimaxFiveHourExpireAt;
    status->minimax.weekExpireAt = storage->minimaxWeekExpireAt;
    for (uint8_t i = 0; i < status->codexResetCount && i < 4; ++i) {
        status->codexResets[i].name = storage->codexResetNames[i];
        status->codexResets[i].expireAt = storage->codexResetExpireAts[i];
    }
}

void publish_wifi_status() {
    StateLock lock;
    if (!lock.locked()) return;
    s_wifi_status = s_wifi_working;
}

void publish_time_state() {
    StateLock lock;
    if (!lock.locked()) return;
    s_published_time.ready = s_time_synced;
    s_published_time.baseMs = s_time_base_ms;
    s_published_time.baseEpoch = s_time_base_epoch;
}

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
    s_wifi_working.ssidVisible = (found > 0);
    s_wifi_working.rssi = (found > 0) ? (int8_t)WiFi.RSSI(0) : 0;
    Serial.printf("[D][AI][WIFI] scan ssid='%s' found=%d rssi=%d\n",
                   DESKNEST_WIFI_SSID, found, (int)s_wifi_working.rssi);
    WiFi.scanDelete();
}

void apply_server_time_basis(const char* server_now_text) {
    const time_t epoch = dn_apply_server_now_boot_offset(dn_parse_iso8601_epoch(server_now_text));
    if (epoch <= 0) return;
    s_time_synced = true;
    s_time_base_epoch = epoch;
    s_time_base_ms = millis();
    publish_time_state();

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
    publish_time_state();
    Serial.printf("[D][AI][TIME] synced %04d-%02d-%02d %02d:%02d:%02d\n",
                  tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                  tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

bool refresh_wifi_link(uint32_t now, bool request_connect) {
    s_wifi_working = {};
    s_wifi_working.configured = wifi_configured();
    if (!s_wifi_working.configured) {
        s_wifi_working.state = AI_WIFI_UNCONFIGURED;
        return false;
    }

    ensure_wifi_station_started();
    wl_status_t st = WiFi.status();
    s_wifi_working.rawStatus = (uint8_t)st;

    if (st == WL_CONNECTED) {
        s_wifi_working.state = AI_WIFI_CONNECTED;
        s_wifi_working.ssidVisible = true;
        s_wifi_working.rssi = (int8_t)WiFi.RSSI();
        s_wifi_working.retryInSec = 0;
        ensure_network_time(now);
        return true;
    }

    if (st == WL_NO_SSID_AVAIL) {
        s_wifi_working.state = AI_WIFI_NO_SSID;
        refresh_wifi_scan(now);
    } else if (st == WL_CONNECT_FAILED) {
        s_wifi_working.state = AI_WIFI_AUTH_FAILED;
    } else if (st == WL_IDLE_STATUS) {
        s_wifi_working.state = AI_WIFI_CONNECTING;
    } else {
        s_wifi_working.state = AI_WIFI_DISCONNECTED;
    }

    const uint32_t cooldown = (st == WL_NO_SSID_AVAIL) ? kWiFiNoSsidCooldownMs : kWiFiConnectCooldownMs;
    s_wifi_working.retryInSec = seconds_until(now, s_last_wifi_attempt_ms, cooldown);

    if (!request_connect || !should_retry_connect(now, st)) {
        return false;
    }

    s_last_wifi_attempt_ms = now;
    s_wifi_working.retryInSec = seconds_until(now, s_last_wifi_attempt_ms, cooldown);
    Serial.printf("[D][AI][WIFI] connect ssid='%s' status=%d\n", DESKNEST_WIFI_SSID, (int)st);
    WiFi.disconnect(false, false);
    WiFi.begin(DESKNEST_WIFI_SSID, DESKNEST_WIFI_PASS);

    const uint32_t deadline = millis() + 1500;
    wl_status_t cur = WiFi.status();
    while (cur == WL_IDLE_STATUS && millis() < deadline) {
        delay(50);
        cur = WiFi.status();
    }

    s_wifi_working.rawStatus = (uint8_t)cur;
    if (cur == WL_CONNECTED) {
        s_wifi_working.state = AI_WIFI_CONNECTED;
        s_wifi_working.ssidVisible = true;
        s_wifi_working.rssi = (int8_t)WiFi.RSSI();
        s_wifi_working.retryInSec = 0;
        ensure_network_time(now);
        Serial.printf("[D][AI][WIFI] connected ip=%s rssi=%d\n",
                           WiFi.localIP().toString().c_str(), (int)s_wifi_working.rssi);
        return true;
    }

    if (cur == WL_NO_SSID_AVAIL) {
        s_wifi_working.state = AI_WIFI_NO_SSID;
        refresh_wifi_scan(now);
    } else if (cur == WL_CONNECT_FAILED) {
        s_wifi_working.state = AI_WIFI_AUTH_FAILED;
    } else {
        s_wifi_working.state = AI_WIFI_CONNECTING;
    }
    s_wifi_working.retryInSec = seconds_until(now, s_last_wifi_attempt_ms, cooldown);
    Serial.printf("[D][AI][WIFI] pending/fail status=%d retry=%us visible=%d\n",
                   (int)cur, (unsigned)s_wifi_working.retryInSec, s_wifi_working.ssidVisible ? 1 : 0);
    return false;
}

bool ensure_wifi(uint32_t now) {
    return refresh_wifi_link(now, true);
}

bool fetch_tokennest_status(AIUsageStatus* out, AIUsageParseStorage* storage, uint32_t now) {
    if (!out || !storage) return false;
    if (!tokennest_configured()) {
        Serial.println("[D][AI][HTTP] skip: TokenNest URL not configured");
        return false;
    }
    if (!ensure_wifi(now)) return false;

    static char json[1536];
    HTTPClient http;
    http.setTimeout(2500);
    Serial.println("[D][AI][HTTP] GET configured status endpoint");
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
    const bool parsed = dn_ai_usage_parse_cc_switch_status(json, storage, out);
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
    static AIUsageParseStorage fallback_storage;
    static AIUsageStatus fallback = dn_ai_usage_demo_status();
    StateLock lock(kSnapshotLockWaitTicks);
    if (!lock.locked()) return fallback;
    const uint32_t now = millis();
    const uint32_t refresh_ms = s_data_ready ? kRefreshIntervalMs : kBootstrapRetryMs;
    AIUsageStatus current = s_cached;
    if (s_live_data_ready) {
        fallback_storage = s_parse_storage[s_active_parse_storage];
        bind_parsed_storage(&current, &fallback_storage);
    }
    const uint32_t elapsed = now >= s_last_load_ms ? now - s_last_load_ms : 0;
    current.nextRefreshInSec = elapsed >= refresh_ms
        ? 0
        : (uint16_t)((refresh_ms - elapsed + 999) / 1000);
    fallback = current;
    return fallback;
}

AIWiFiStatus dn_ai_usage_wifi_status() {
    static AIWiFiStatus fallback;
    StateLock lock(kSnapshotLockWaitTicks);
    if (!lock.locked()) return fallback;
    fallback = s_wifi_status;
    return fallback;
}

void dn_ai_usage_service_begin() {
    ensure_state_mutex();
    const uint32_t now = millis();
    ensure_wifi_station_started();
    refresh_wifi_link(now, true);
    publish_wifi_status();
}

void dn_ai_usage_service_tick() {
    // Network calls can block for seconds. Keep them entirely outside the
    // snapshot mutex so the main loop can continue sampling input and pumping
    // LVGL; only the small state copies below take StateLock.
    const uint32_t now = millis();
    refresh_wifi_link(now, true);
    publish_wifi_status();

    bool loaded_once = false;
    bool data_ready = false;
    uint32_t last_load_ms = 0;
    uint8_t next_storage = 0;
    {
        StateLock lock;
        if (lock.locked()) {
            loaded_once = s_loaded_once;
            data_ready = s_data_ready;
            last_load_ms = s_last_load_ms;
            next_storage = (uint8_t)(s_active_parse_storage ^ 1U);
        }
    }

    const uint32_t refresh_ms = data_ready ? kRefreshIntervalMs : kBootstrapRetryMs;
    if (!loaded_once || now - last_load_ms >= refresh_ms) {
        AIUsageStatus parsed;
        Serial.printf("[D][AI] refresh start loaded=%d age=%lums\n",
                      loaded_once ? 1 : 0,
                      (unsigned long)(loaded_once ? now - last_load_ms : 0));
        const bool fetched = fetch_tokennest_status(
            &parsed, &s_parse_storage[next_storage], now);
        publish_wifi_status();
        {
            StateLock lock;
            if (lock.locked()) {
                if (fetched) {
                    s_cached = parsed;
                    s_active_parse_storage = next_storage;
                    s_data_ready = true;
                    s_live_data_ready = true;
                } else if (!loaded_once) {
                    s_cached = dn_ai_usage_demo_status();
                    s_data_ready = false;
                    s_live_data_ready = false;
                }
                s_loaded_once = true;
                s_last_load_ms = now;
            }
        }
        if (fetched) {
            Serial.println("[D][AI] source=http");
        } else if (!loaded_once) {
            Serial.println("[D][AI] source=demo");
        } else {
            Serial.println("[D][AI] source=previous");
        }
    }
}

const char* dn_ai_usage_time_text() {
    static char s_time_text[8] = "";
    StateLock lock(kSnapshotLockWaitTicks);
    if (!lock.locked()) return s_time_text;
    if (!s_published_time.ready || s_published_time.baseEpoch <= 0) return "";

    const uint32_t now_ms = millis();
    const time_t epoch = s_published_time.baseEpoch
        + (time_t)((now_ms - s_published_time.baseMs) / 1000UL);
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
    StateLock lock(kSnapshotLockWaitTicks);
    if (!lock.locked()) return 0;
    if (!s_published_time.ready || s_published_time.baseEpoch <= 0) return 0;
    const uint32_t now_ms = millis();
    return s_published_time.baseEpoch
        + (time_t)((now_ms - s_published_time.baseMs) / 1000UL);
}

bool dn_ai_usage_time_ready() {
    StateLock lock(kSnapshotLockWaitTicks);
    if (!lock.locked()) return false;
    return s_published_time.ready && s_published_time.baseEpoch > 0;
}

bool dn_ai_usage_data_ready() {
    StateLock lock(kSnapshotLockWaitTicks);
    if (!lock.locked()) return false;
    return s_data_ready;
}

bool dn_ai_usage_live_data_ready() {
    StateLock lock(kSnapshotLockWaitTicks);
    if (!lock.locked()) return false;
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
