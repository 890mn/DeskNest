#include "desk_remote_config.h"
#include "config.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

namespace desknest {
namespace {
DeskRemoteConfig s_config;
uint32_t s_last_fetch_ms = 0;
constexpr uint32_t kRefreshMs = 30000;

bool desk_url(char* out, size_t cap) {
    const char* source = DESKNEST_TOKENNEST_STATUS_URL;
    if (!source || !source[0]) return false;
    const char* slash = strrchr(source, '/');
    if (!slash) return false;
    const size_t rootLen = (size_t)(slash - source);
    if (rootLen + strlen("/api/desknest") + 1 > cap) return false;
    memcpy(out, source, rootLen);
    strcpy(out + rootLen, "/api/desknest");
    return true;
}
}

void dn_desk_remote_config_begin() { s_last_fetch_ms = 0; }

void dn_desk_remote_config_tick() {
    const uint32_t now = millis();
    if (now - s_last_fetch_ms < kRefreshMs || WiFi.status() != WL_CONNECTED) return;
    s_last_fetch_ms = now;
    char url[180] = {};
    if (!desk_url(url, sizeof(url))) return;
    HTTPClient http;
    if (!http.begin(url)) return;
    const int code = http.GET();
    if (code == HTTP_CODE_OK) {
        const String payload = http.getString();
        DeskRemoteConfig parsed;
        if (dn_desk_config_parse(payload.c_str(), &parsed)) {
            s_config = parsed;
            Serial.printf("[D][DESK] menu synced items=%s\n", s_config.items[0].name);
        }
    }
    http.end();
}

const DeskRemoteConfig& dn_desk_remote_config() { return s_config; }

} // namespace desknest
