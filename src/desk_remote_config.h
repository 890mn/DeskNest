#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace desknest {

struct DeskMenuItem {
    char name[32] = {};
    char price[10] = {};
    uint8_t score = 0;
    bool active = false;
};

struct DeskRemoteConfig {
    bool ready = false;
    char today[64] = {};
    char yesterday[64] = {};
    DeskMenuItem items[5] = {};
};

inline bool dn_desk_json_string(const char* text, const char* key, char* out, size_t cap) {
    if (!text || !key || !out || cap < 2) return false;
    char marker[40];
    snprintf(marker, sizeof(marker), "\"%s\"", key);
    const char* p = strstr(text, marker);
    if (!p || !(p = strchr(p + strlen(marker), ':'))) return false;
    p = strchr(p, '"');
    if (!p) return false;
    ++p;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < cap) out[n++] = *p++;
    out[n] = '\0';
    return n > 0;
}

inline int dn_desk_json_int(const char* text, const char* key, int fallback) {
    char marker[40];
    snprintf(marker, sizeof(marker), "\"%s\"", key);
    const char* p = text ? strstr(text, marker) : nullptr;
    if (!p || !(p = strchr(p + strlen(marker), ':'))) return fallback;
    return atoi(p + 1);
}

inline bool dn_desk_config_parse(const char* json, DeskRemoteConfig* out) {
    if (!json || !out) return false;
    DeskRemoteConfig next = {};
    if (!dn_desk_json_string(json, "today", next.today, sizeof(next.today))) return false;
    if (!dn_desk_json_string(json, "yesterday", next.yesterday, sizeof(next.yesterday))) return false;
    const char* items = strstr(json, "\"items\"");
    if (!items) return false;
    const char* p = strchr(items, '[');
    if (!p) return false;
    for (int i = 0; i < 5; ++i) {
        p = strchr(p, '{');
        if (!p) break;
        const char* end = strchr(p, '}');
        if (!end) break;
        char itemJson[160] = {};
        const size_t len = (size_t)(end - p + 1) < sizeof(itemJson) - 1 ? (size_t)(end - p + 1) : sizeof(itemJson) - 1;
        memcpy(itemJson, p, len);
        dn_desk_json_string(itemJson, "name", next.items[i].name, sizeof(next.items[i].name));
        dn_desk_json_string(itemJson, "price", next.items[i].price, sizeof(next.items[i].price));
        next.items[i].score = (uint8_t)dn_desk_json_int(itemJson, "score", 0);
        next.items[i].active = strstr(itemJson, "\"active\":true") != nullptr;
        p = end + 1;
    }
    next.ready = next.items[0].name[0] != '\0';
    if (!next.ready) return false;
    *out = next;
    return true;
}

void dn_desk_remote_config_begin();
void dn_desk_remote_config_tick();
#ifdef UNIT_TEST
inline const DeskRemoteConfig& dn_desk_remote_config() {
    static DeskRemoteConfig empty;
    return empty;
}
#else
const DeskRemoteConfig& dn_desk_remote_config();
#endif

} // namespace desknest
