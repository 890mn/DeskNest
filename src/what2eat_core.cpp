#include "what2eat_module.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace desknest {
namespace {

const char* core_find_key(const char* text, const char* key) {
    if (!text || !key) return nullptr;
    char marker[48];
    snprintf(marker, sizeof(marker), "\"%s\"", key);
    const char* p = strstr(text, marker);
    if (!p) return nullptr;
    p = strchr(p + strlen(marker), ':');
    return p ? p + 1 : nullptr;
}

bool core_json_uint(const char* text, const char* key, uint32_t* out) {
    const char* p = core_find_key(text, key);
    if (!p || !out) return false;
    while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
    if (!isdigit(static_cast<unsigned char>(*p))) return false;
    char* end = nullptr;
    const unsigned long value = strtoul(p, &end, 10);
    if (end == p || value > UINT32_MAX) return false;
    *out = static_cast<uint32_t>(value);
    return true;
}

bool core_json_string(const char* text, const char* key, char* out, size_t cap) {
    const char* p = core_find_key(text, key);
    if (!p || !out || cap < 2) return false;
    while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
    if (*p++ != '"') return false;
    size_t n = 0;
    while (*p && *p != '"') {
        // The local service owns normalization. Reject escapes instead of
        // accepting a partially decoded or truncated cache record.
        if (*p == '\\' || static_cast<unsigned char>(*p) < 0x20 || n + 1 >= cap) return false;
        out[n++] = *p++;
    }
    if (*p != '"' || n == 0) return false;
    out[n] = '\0';
    return true;
}

bool core_valid_hash(const char* hash) {
    if (!hash || strlen(hash) != 64) return false;
    for (const char* p = hash; *p; ++p) {
        if (!isxdigit(static_cast<unsigned char>(*p))) return false;
    }
    return true;
}

bool core_unique_id(const What2EatSnapshot& snapshot, const char* id) {
    for (uint8_t i = 0; i < snapshot.itemCount; ++i) {
        if (strcmp(snapshot.items[i].id, id) == 0) return false;
    }
    return true;
}

} // namespace

bool dn_what2eat_parse_envelope(const char* json, What2EatSnapshot* out) {
    if (!json || !out) return false;
    What2EatSnapshot candidate;
    uint32_t value = 0;
    if (!core_json_uint(json, "schemaVersion", &value) ||
        value != WHAT2EAT_SCHEMA_VERSION) return false;
    candidate.schemaVersion = static_cast<uint16_t>(value);
    if (!core_json_uint(json, "revision", &candidate.revision) ||
        candidate.revision == 0) return false;
    if (!core_json_string(json, "contentHash", candidate.contentHash,
                          sizeof(candidate.contentHash)) ||
        !core_valid_hash(candidate.contentHash)) return false;

    const char* what2eat = strstr(json, "\"what2eat\"");
    const char* itemsKey = what2eat ? strstr(what2eat, "\"items\"") : nullptr;
    const char* p = itemsKey ? strchr(itemsKey, '[') : nullptr;
    if (!p) return false;
    ++p;
    while (candidate.itemCount < WHAT2EAT_MAX_ITEMS) {
        while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
        if (*p == ']') break;
        if (*p != '{') return false;
        const char* end = strchr(p, '}');
        if (!end) return false;
        const size_t length = static_cast<size_t>(end - p + 1);
        if (length >= 256) return false;
        char itemJson[256] = {};
        memcpy(itemJson, p, length);

        What2EatItem item;
        uint32_t count = 0;
        uint32_t score = 0;
        if (!core_json_string(itemJson, "id", item.id, sizeof(item.id)) ||
            !core_unique_id(candidate, item.id) ||
            !core_json_string(itemJson, "name", item.name, sizeof(item.name)) ||
            item.name[0] == '\0' ||
            !core_json_uint(itemJson, "count", &count) || count > UINT16_MAX ||
            !core_json_string(itemJson, "price", item.price, sizeof(item.price)) ||
            !core_json_uint(itemJson, "score", &score) ||
            score < WHAT2EAT_SCORE_MIN_TENTHS ||
            score > WHAT2EAT_SCORE_MAX_TENTHS ||
            score % WHAT2EAT_SCORE_STEP_TENTHS != 0) {
            return false;
        }
        item.count = static_cast<uint16_t>(count);
        item.score = static_cast<uint8_t>(score);
        candidate.items[candidate.itemCount++] = item;
        p = end + 1;
        while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
        if (*p == ',') ++p;
    }
    while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
    if (candidate.itemCount == 0 || *p != ']') return false;
    candidate.selectedIndex = -1;
    candidate.state = WHAT2EAT_APPLIED;
    *out = candidate;
    return true;
}

bool dn_what2eat_candidate_is_newer(const What2EatSnapshot& current,
                                    const What2EatSnapshot& candidate) {
    return candidate.schemaVersion == WHAT2EAT_SCHEMA_VERSION &&
           candidate.revision > current.revision && candidate.itemCount > 0 &&
           candidate.itemCount <= WHAT2EAT_MAX_ITEMS;
}

bool dn_what2eat_ack_needed(uint32_t cachedRevision, uint32_t ackedRevision) {
    return cachedRevision > 0 && cachedRevision > ackedRevision;
}

int8_t dn_what2eat_pick_index(uint8_t itemCount, int8_t currentIndex,
                              uint32_t entropy) {
    if (itemCount == 0 || itemCount > WHAT2EAT_MAX_ITEMS) return -1;
    int8_t next = static_cast<int8_t>(entropy % itemCount);
    if (itemCount > 1 && next == currentIndex) {
        next = static_cast<int8_t>((next + 1) % itemCount);
    }
    return next;
}

} // namespace desknest
