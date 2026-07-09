// src/ai_usage_module.h
// 栖屏 DeskNest - AI usage status module
//
// UI model 不需要改，只替换模块数据来源。

#ifndef DESKNEST_AI_USAGE_MODULE_H
#define DESKNEST_AI_USAGE_MODULE_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

namespace desknest {

enum AIWiFiState : uint8_t {
    AI_WIFI_UNCONFIGURED = 0,
    AI_WIFI_DISCONNECTED,
    AI_WIFI_CONNECTING,
    AI_WIFI_CONNECTED,
    AI_WIFI_NO_SSID,
    AI_WIFI_AUTH_FAILED,
};

struct AIWiFiStatus {
    AIWiFiState state = AI_WIFI_UNCONFIGURED;
    bool configured = false;
    bool ssidVisible = false;
    int8_t rssi = 0;
    uint8_t rawStatus = 255;
    uint16_t retryInSec = 0;
};

struct AIUsageItemStatus {
    const char* name = "";
    uint8_t percent = 0;
    uint8_t weeklyPercent = 0;
    const char* statusText = "";
    const char* detailText = "";
    const char* fiveHourExpireAt = "";
    const char* weekExpireAt = "";
};

struct AICodexResetStatus {
    const char* name = "";
    const char* expireAt = "";
};

struct AIUsageStatus {
    uint8_t totalPercent = 0;
    AIUsageItemStatus minimax;
    AIUsageItemStatus codex;
    AIUsageItemStatus chatgpt;
    AICodexResetStatus codexResets[4];
    uint8_t codexResetCount = 0;
    const char* updatedAtText = "";
    const char* warningText = "";
    const char* serverNow = "";
    uint16_t nextRefreshInSec = 0;
    bool fromCache = false;
};

inline uint8_t dn_clamp_percent(int value) {
    if (value < 0) return 0;
    if (value > 100) return 100;
    return (uint8_t)value;
}

inline int64_t dn_days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

inline time_t dn_parse_iso8601_epoch(const char* text) {
    if (!text || !text[0]) return 0;
    int y = 0, mon = 0, d = 0, h = 0, min = 0, s = 0;
    char sign = '\0';
    int off_h = 0, off_m = 0;
    if (sscanf(text, "%d-%d-%dT%d:%d:%d%c%d:%d",
               &y, &mon, &d, &h, &min, &s, &sign, &off_h, &off_m) < 7) {
        return 0;
    }
    int64_t epoch = dn_days_from_civil(y, (unsigned)mon, (unsigned)d) * 86400LL
        + h * 3600LL + min * 60LL + s;
    const int offset_sec = off_h * 3600 + off_m * 60;
    if (sign == '+') epoch -= offset_sec;
    else if (sign == '-') epoch += offset_sec;
    return epoch > 0 ? (time_t)epoch : 0;
}

inline time_t dn_apply_server_now_boot_offset(time_t epoch) {
    return epoch > 0 ? (epoch + 8 * 3600) : 0;
}

inline AIUsageItemStatus dn_ai_usage_item(const char* name,
                                          int percent,
                                          const char* statusText,
                                          const char* detailText,
                                          int weeklyPercent = 0,
                                          const char* fiveHourExpireAt = "",
                                          const char* weekExpireAt = "") {
    AIUsageItemStatus item;
    item.name = name;
    item.percent = dn_clamp_percent(percent);
    item.weeklyPercent = dn_clamp_percent(weeklyPercent);
    item.statusText = statusText;
    item.detailText = detailText;
    item.fiveHourExpireAt = fiveHourExpireAt;
    item.weekExpireAt = weekExpireAt;
    return item;
}

inline AIUsageStatus dn_ai_usage_demo_status() {
    AIUsageStatus status;
    status.totalPercent = 72;
    status.chatgpt = dn_ai_usage_item("ChatGPT", 72, "OK", "5h:72% wk:11%", 11);
    status.codex = dn_ai_usage_item("Codex", 58, "正常", "");
    status.minimax = dn_ai_usage_item("MiniMax", 86, "充足", "");
    status.minimax.weeklyPercent = 18;
    status.updatedAtText = "cached";
    status.warningText = "";
    status.nextRefreshInSec = 30;
    status.fromCache = false;
    return status;
}

struct AIUsageParseStorage {
    char updatedAtText[32] = "";
    char warningText[48] = "";
    char serverNow[40] = "";
    char chatgptStatus[24] = "";
    char chatgptDetail[48] = "";
    char minimaxStatus[24] = "";
    char minimaxDetail[48] = "";
    char chatgptFiveHourExpireAt[40] = "";
    char chatgptWeekExpireAt[40] = "";
    char minimaxFiveHourExpireAt[40] = "";
    char minimaxWeekExpireAt[40] = "";
    char codexResetNames[4][24] = {};
    char codexResetExpireAts[4][40] = {};
};

inline const char* dn_json_find_key(const char* json, const char* key) {
    if (!json || !key) return nullptr;
    char pattern[32];
    const int n = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (n <= 0 || n >= (int)sizeof(pattern)) return nullptr;
    return strstr(json, pattern);
}

inline bool dn_json_string_value(const char* json, const char* key, char* out, size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = '\0';
    const char* p = dn_json_find_key(json, key);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != '"') return false;
    ++p;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap) {
        if (*p == '\\' && p[1]) ++p;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

inline bool dn_json_int_value(const char* json, const char* key, int* out) {
    if (!out) return false;
    const char* p = dn_json_find_key(json, key);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    bool neg = false;
    if (*p == '-') {
        neg = true;
        ++p;
    }
    if (*p < '0' || *p > '9') return false;
    int value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        ++p;
    }
    *out = neg ? -value : value;
    return true;
}

inline bool dn_parse_percent_after_marker(const char* text, const char* marker, int* out) {
    if (!text || !marker || !out) return false;
    const char* p = strstr(text, marker);
    if (!p) return false;
    p += strlen(marker);
    while (*p == ' ' || *p == '\t' || *p == ':') ++p;
    if (*p < '0' || *p > '9') return false;
    int value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        ++p;
    }
    *out = value;
    return true;
}

inline int dn_weekly_percent_from_detail(const char* detail) {
    int weekly = 0;
    if (dn_parse_percent_after_marker(detail, "wk", &weekly) ||
        dn_parse_percent_after_marker(detail, "weekly", &weekly) ||
        dn_parse_percent_after_marker(detail, "week", &weekly)) {
        return weekly;
    }
    return 0;
}

inline const char* dn_json_object_for_service(const char* json, const char* service) {
    const char* p = dn_json_find_key(json, service);
    if (!p) return nullptr;
    const char* obj = strchr(p, '{');
    return obj;
}

inline void dn_parse_service_usage(const char* json,
                                   const char* key,
                                   const char* displayName,
                                   char* statusBuf,
                                   size_t statusCap,
                                   char* detailBuf,
                                   size_t detailCap,
                                   char* fiveHourExpireAtBuf,
                                   size_t fiveHourExpireAtCap,
                                   char* weekExpireAtBuf,
                                   size_t weekExpireAtCap,
                                   AIUsageItemStatus* out) {
    const char* obj = dn_json_object_for_service(json, key);
    int percent = 0;
    if (!obj || !dn_json_int_value(obj, "percent", &percent)) {
        percent = 0;
    }
    int weeklyPercent = 0;
    if (obj) {
        if (!dn_json_string_value(obj, "status", statusBuf, statusCap)) {
            statusBuf[0] = '\0';
        }
        if (!dn_json_string_value(obj, "detail", detailBuf, detailCap) &&
            !dn_json_string_value(obj, "details", detailBuf, detailCap)) {
            detailBuf[0] = '\0';
        }
        if (!dn_json_string_value(obj, "fiveHourExpireAt", fiveHourExpireAtBuf, fiveHourExpireAtCap)) {
            fiveHourExpireAtBuf[0] = '\0';
        }
        if (!dn_json_string_value(obj, "weekExpireAt", weekExpireAtBuf, weekExpireAtCap)) {
            weekExpireAtBuf[0] = '\0';
        }
        weeklyPercent = dn_weekly_percent_from_detail(detailBuf);
        if (weeklyPercent == 0 &&
            !dn_json_int_value(obj, "weeklyPercent", &weeklyPercent) &&
            !dn_json_int_value(obj, "secondaryPercent", &weeklyPercent)) {
            weeklyPercent = 0;
        }
    } else {
        statusBuf[0] = '\0';
        detailBuf[0] = '\0';
        fiveHourExpireAtBuf[0] = '\0';
        weekExpireAtBuf[0] = '\0';
    }
    *out = dn_ai_usage_item(displayName, percent, statusBuf, detailBuf, weeklyPercent,
                            fiveHourExpireAtBuf, weekExpireAtBuf);
}

inline uint8_t dn_parse_codex_resets(const char* json, AIUsageParseStorage* storage, AICodexResetStatus* out, uint8_t cap) {
    if (!json || !storage || !out || cap == 0) return 0;
    const char* key = dn_json_find_key(json, "codexResets");
    if (!key) return 0;
    const char* arr = strchr(key, '[');
    if (!arr) return 0;
    ++arr;
    uint8_t count = 0;
    while (*arr && *arr != ']' && count < cap) {
        const char* obj_start = strchr(arr, '{');
        if (!obj_start || (*arr == ']' && obj_start > arr)) break;
        const char* obj_end = strchr(obj_start, '}');
        if (!obj_end) break;
        char obj_buf[192];
        size_t len = (size_t)(obj_end - obj_start + 1);
        if (len >= sizeof(obj_buf)) len = sizeof(obj_buf) - 1;
        memcpy(obj_buf, obj_start, len);
        obj_buf[len] = '\0';

        storage->codexResetNames[count][0] = '\0';
        storage->codexResetExpireAts[count][0] = '\0';
        dn_json_string_value(obj_buf, "name",
                             storage->codexResetNames[count], sizeof(storage->codexResetNames[count]));
        dn_json_string_value(obj_buf, "expireAt",
                             storage->codexResetExpireAts[count], sizeof(storage->codexResetExpireAts[count]));
        out[count].name = storage->codexResetNames[count];
        out[count].expireAt = storage->codexResetExpireAts[count];
        if (out[count].name[0] || out[count].expireAt[0]) ++count;
        arr = obj_end + 1;
    }
    return count;
}

inline bool dn_ai_usage_parse_cc_switch_status(const char* json,
                                               AIUsageParseStorage* storage,
                                               AIUsageStatus* out) {
    if (!json || !storage || !out) return false;
    AIUsageStatus status;

    dn_json_string_value(json, "updatedAtText", storage->updatedAtText, sizeof(storage->updatedAtText)) ||
        dn_json_string_value(json, "updatedAt", storage->updatedAtText, sizeof(storage->updatedAtText));
    dn_json_string_value(json, "warningText", storage->warningText, sizeof(storage->warningText)) ||
        dn_json_string_value(json, "warning", storage->warningText, sizeof(storage->warningText));
    dn_json_string_value(json, "serverNow", storage->serverNow, sizeof(storage->serverNow));

    dn_parse_service_usage(json, "chatgpt", "ChatGPT",
                           storage->chatgptStatus, sizeof(storage->chatgptStatus),
                           storage->chatgptDetail, sizeof(storage->chatgptDetail),
                           storage->chatgptFiveHourExpireAt, sizeof(storage->chatgptFiveHourExpireAt),
                           storage->chatgptWeekExpireAt, sizeof(storage->chatgptWeekExpireAt),
                           &status.chatgpt);
    dn_parse_service_usage(json, "minimax", "MiniMax",
                           storage->minimaxStatus, sizeof(storage->minimaxStatus),
                           storage->minimaxDetail, sizeof(storage->minimaxDetail),
                           storage->minimaxFiveHourExpireAt, sizeof(storage->minimaxFiveHourExpireAt),
                           storage->minimaxWeekExpireAt, sizeof(storage->minimaxWeekExpireAt),
                           &status.minimax);
    status.codex = dn_ai_usage_item("Codex", 0, "", "");
    status.codexResetCount = dn_parse_codex_resets(json, storage, status.codexResets, 4);

    int total = -1;
    if (dn_json_int_value(json, "totalPercent", &total)) {
        status.totalPercent = dn_clamp_percent(total);
    } else {
        status.totalPercent = status.chatgpt.percent > status.minimax.percent
            ? status.chatgpt.percent
            : status.minimax.percent;
    }

    int nextRefresh = 0;
    if (dn_json_int_value(json, "nextRefreshInSec", &nextRefresh)) {
        if (nextRefresh < 0) nextRefresh = 0;
        if (nextRefresh > 65535) nextRefresh = 65535;
        status.nextRefreshInSec = (uint16_t)nextRefresh;
    }
    status.updatedAtText = storage->updatedAtText[0] ? storage->updatedAtText : "cached";
    status.warningText = storage->warningText;
    status.serverNow = storage->serverNow;
    status.fromCache = true;
    *out = status;
    return true;
}

AIUsageStatus dn_ai_usage_status();
AIWiFiStatus dn_ai_usage_wifi_status();
void dn_ai_usage_service_begin();
void dn_ai_usage_service_tick();
const char* dn_ai_usage_time_text();
time_t dn_ai_usage_now_epoch();
bool dn_ai_usage_time_ready();
bool dn_ai_usage_data_ready();
bool dn_ai_usage_live_data_ready();

} // namespace desknest

#endif // DESKNEST_AI_USAGE_MODULE_H
