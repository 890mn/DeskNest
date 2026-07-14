#include "what2eat_module.h"
#include "config.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mbedtls/sha256.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace desknest {
namespace {

constexpr uint32_t kSyncIntervalMs = 5000;
constexpr uint32_t kHttpTimeoutMs = 1500;
constexpr uint32_t kCacheMagic = 0x57324531UL; // W2E1
constexpr size_t kMaxSyncPayloadBytes = 4096;
constexpr const char* kNvsNamespace = "dn_what2eat";
constexpr const char* kSlotA = "slot_a";
constexpr const char* kSlotB = "slot_b";
constexpr const char* kActiveSlot = "active";
constexpr const char* kAckedRevision = "acked_rev";
constexpr uint32_t kSnapshotWaitMs = 5;

struct PersistedWhat2Eat {
    uint32_t magic = kCacheMagic;
    uint16_t schemaVersion = WHAT2EAT_SCHEMA_VERSION;
    uint32_t revision = 0;
    char contentHash[65] = {};
    What2EatItem items[WHAT2EAT_MAX_ITEMS] = {};
    uint8_t itemCount = 0;
    uint32_t checksum = 0;
};

What2EatSnapshot s_snapshot;
SemaphoreHandle_t s_mutex = nullptr;
uint32_t s_last_sync_ms = 0;
uint32_t s_acked_revision = 0;

// The fifteen-item snapshot is intentionally kept out of the remote task's
// stack. These scratch buffers are owned by the single remote worker, so the
// parser/sync path can reuse them without allocating or copying large objects
// on every poll.
What2EatSnapshot s_sync_before;
What2EatSnapshot s_sync_candidate;
PersistedWhat2Eat s_cache_value;
PersistedWhat2Eat s_cache_verified;

void ensure_mutex() {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
}

struct SnapshotLock {
    SnapshotLock() {
        ensure_mutex();
        if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
    ~SnapshotLock() {
        if (s_mutex) xSemaphoreGive(s_mutex);
    }
};

uint32_t fnv1a(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

uint32_t persisted_checksum(const PersistedWhat2Eat& value) {
    return fnv1a(&value, offsetof(PersistedWhat2Eat, checksum));
}

bool terminated(const char* text, size_t cap) {
    return text && memchr(text, '\0', cap) != nullptr;
}

bool valid_persisted(const PersistedWhat2Eat& value) {
    if (value.magic != kCacheMagic ||
        value.schemaVersion != WHAT2EAT_SCHEMA_VERSION ||
        value.revision == 0 || value.itemCount == 0 ||
        value.itemCount > WHAT2EAT_MAX_ITEMS ||
        value.checksum != persisted_checksum(value) ||
        !terminated(value.contentHash, sizeof(value.contentHash))) {
        return false;
    }
    for (uint8_t i = 0; i < value.itemCount; ++i) {
        if (!terminated(value.items[i].id, sizeof(value.items[i].id)) ||
            !terminated(value.items[i].name, sizeof(value.items[i].name)) ||
            !terminated(value.items[i].price, sizeof(value.items[i].price)) ||
            value.items[i].id[0] == '\0' || value.items[i].name[0] == '\0' ||
            value.items[i].score < WHAT2EAT_SCORE_MIN_TENTHS ||
            value.items[i].score > WHAT2EAT_SCORE_MAX_TENTHS ||
            value.items[i].score % WHAT2EAT_SCORE_STEP_TENTHS != 0) {
            return false;
        }
    }
    return true;
}

PersistedWhat2Eat persisted_from_snapshot(const What2EatSnapshot& snapshot) {
    PersistedWhat2Eat value;
    value.revision = snapshot.revision;
    memcpy(value.contentHash, snapshot.contentHash, sizeof(value.contentHash));
    memcpy(value.items, snapshot.items, sizeof(value.items));
    value.itemCount = snapshot.itemCount;
    value.checksum = persisted_checksum(value);
    return value;
}

What2EatSnapshot snapshot_from_persisted(const PersistedWhat2Eat& value) {
    What2EatSnapshot snapshot;
    snapshot.state = WHAT2EAT_CACHED;
    snapshot.schemaVersion = value.schemaVersion;
    snapshot.revision = value.revision;
    memcpy(snapshot.contentHash, value.contentHash, sizeof(snapshot.contentHash));
    memcpy(snapshot.items, value.items, sizeof(snapshot.items));
    snapshot.itemCount = value.itemCount;
    snapshot.selectedIndex = dn_what2eat_pick_index(snapshot.itemCount, -1, esp_random());
    return snapshot;
}

bool read_slot(Preferences& prefs, const char* key, PersistedWhat2Eat* out) {
    if (!out || prefs.getBytesLength(key) != sizeof(PersistedWhat2Eat)) return false;
    PersistedWhat2Eat value;
    if (prefs.getBytes(key, &value, sizeof(value)) != sizeof(value) || !valid_persisted(value)) {
        return false;
    }
    *out = value;
    return true;
}

bool load_cache(What2EatSnapshot* out) {
    if (!out) return false;
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) return false;
    PersistedWhat2Eat a;
    PersistedWhat2Eat b;
    const bool hasA = read_slot(prefs, kSlotA, &a);
    const bool hasB = read_slot(prefs, kSlotB, &b);
    const uint8_t active = prefs.getUChar(kActiveSlot, 0xff);
    prefs.end();
    if (!hasA && !hasB) return false;
    const PersistedWhat2Eat& selected =
        (active == 0 && hasA) ? a :
        (active == 1 && hasB) ? b :
        (!hasB || (hasA && a.revision >= b.revision)) ? a : b;
    *out = snapshot_from_persisted(selected);
    return true;
}

bool save_cache(const What2EatSnapshot& snapshot) {
    if (snapshot.revision == 0 || snapshot.itemCount == 0) return false;
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) return false;
    const uint8_t active = prefs.getUChar(kActiveSlot, 0);
    const uint8_t next = active == 0 ? 1 : 0;
    const char* key = next == 0 ? kSlotA : kSlotB;
    s_cache_value = persisted_from_snapshot(snapshot);
    const bool wrote = prefs.putBytes(key, &s_cache_value, sizeof(s_cache_value)) == sizeof(s_cache_value);
    const bool valid = wrote && read_slot(prefs, key, &s_cache_verified) &&
                       s_cache_verified.revision == snapshot.revision;
    const bool activated = valid && prefs.putUChar(kActiveSlot, next) == 1;
    prefs.end();
    return activated;
}

uint32_t load_acked_revision() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) return 0;
    const uint32_t revision = prefs.getUInt(kAckedRevision, 0);
    prefs.end();
    return revision;
}

bool save_acked_revision(uint32_t revision) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) return false;
    const bool saved = prefs.putUInt(kAckedRevision, revision) == sizeof(uint32_t);
    prefs.end();
    return saved;
}

const char* find_key(const char* text, const char* key) {
    if (!text || !key) return nullptr;
    char marker[48];
    snprintf(marker, sizeof(marker), "\"%s\"", key);
    const char* p = strstr(text, marker);
    if (!p) return nullptr;
    p = strchr(p + strlen(marker), ':');
    return p ? p + 1 : nullptr;
}

bool json_uint(const char* text, const char* key, uint32_t* out) {
    const char* p = find_key(text, key);
    if (!p || !out) return false;
    while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
    if (!isdigit(static_cast<unsigned char>(*p))) return false;
    char* end = nullptr;
    const unsigned long value = strtoul(p, &end, 10);
    if (end == p) return false;
    *out = static_cast<uint32_t>(value);
    return true;
}

bool service_url(char* out, size_t cap, const char* suffix) {
    const char* source = DESKNEST_TOKENNEST_STATUS_URL;
    if (!source || !source[0] || !suffix) return false;
    const char* slash = strrchr(source, '/');
    if (!slash) return false;
    const size_t rootLen = static_cast<size_t>(slash - source);
    const size_t suffixLen = strlen(suffix);
    if (rootLen + suffixLen + 1 > cap) return false;
    memcpy(out, source, rootLen);
    memcpy(out + rootLen, suffix, suffixLen + 1);
    return true;
}

bool payload_hash_matches(const char* json, const char* expectedHash) {
    if (!json || !expectedHash || strlen(expectedHash) != 64) return false;
    const char* key = strstr(json, "\"what2eat\"");
    const char* start = key ? strchr(key, '{') : nullptr;
    if (!start) return false;
    const char* p = start;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (; *p; ++p) {
        const char ch = *p;
        if (inString) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') inString = false;
            continue;
        }
        if (ch == '"') inString = true;
        else if (ch == '{') ++depth;
        else if (ch == '}' && --depth == 0) {
            ++p;
            break;
        }
    }
    if (depth != 0 || p <= start) return false;

    uint8_t digest[32];
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    const bool ok = mbedtls_sha256_starts_ret(&context, 0) == 0 &&
                    mbedtls_sha256_update_ret(
                        &context, reinterpret_cast<const unsigned char*>(start),
                        static_cast<size_t>(p - start)) == 0 &&
                    mbedtls_sha256_finish_ret(&context, digest) == 0;
    mbedtls_sha256_free(&context);
    if (!ok) return false;

    char actual[65];
    for (size_t i = 0; i < sizeof(digest); ++i) {
        snprintf(actual + i * 2, 3, "%02x", digest[i]);
    }
    actual[64] = '\0';
    return strcmp(actual, expectedHash) == 0;
}

void restore_after_sync(What2EatState previous) {
    SnapshotLock lock;
    s_snapshot.state = s_snapshot.itemCount == 0
        ? WHAT2EAT_ABSENT
        : (previous == WHAT2EAT_CACHED || previous == WHAT2EAT_FAILED_USING_CACHE
            ? WHAT2EAT_CACHED : WHAT2EAT_APPLIED);
}

void mark_sync_failed() {
    SnapshotLock lock;
    s_snapshot.state = s_snapshot.itemCount > 0
        ? WHAT2EAT_FAILED_USING_CACHE : WHAT2EAT_ABSENT;
}

bool ack_revision(uint32_t revision, const char* status, const char* error = nullptr) {
    char url[192];
    if (!service_url(url, sizeof(url), "/api/what2eat/ack")) {
        Serial.printf("[D][W2E] ack skipped revision=%lu reason=invalid_service_url\n",
                      static_cast<unsigned long>(revision));
        return false;
    }
    char body[192];
    if (error && error[0]) {
        snprintf(body, sizeof(body),
                 "{\"revision\":%lu,\"status\":\"%s\",\"error\":\"%s\"}",
                 static_cast<unsigned long>(revision), status, error);
    } else {
        snprintf(body, sizeof(body), "{\"revision\":%lu,\"status\":\"%s\"}",
                 static_cast<unsigned long>(revision), status);
    }
    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(url)) {
        Serial.printf("[D][W2E] ack failed revision=%lu reason=http_begin\n",
                      static_cast<unsigned long>(revision));
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    const int code = http.POST(String(body));
    http.end();
    const bool ok = code >= 200 && code < 300;
    Serial.printf("[D][W2E] ack %s revision=%lu status=%s http=%d\n",
                  ok ? "sent" : "failed",
                  static_cast<unsigned long>(revision),
                  status ? status : "unknown",
                  code);
    return ok;
}

bool retry_applied_ack(uint32_t revision) {
    if (!dn_what2eat_ack_needed(revision, s_acked_revision)) return true;
    if (!ack_revision(revision, "applied")) return false;
    if (!save_acked_revision(revision)) {
        Serial.printf("[D][W2E] ack persistence failed revision=%lu\n",
                      static_cast<unsigned long>(revision));
        return false;
    }
    s_acked_revision = revision;
    return true;
}

} // namespace

void dn_what2eat_begin() {
    ensure_mutex();
    What2EatSnapshot cached;
    const bool found = load_cache(&cached);
    s_acked_revision = load_acked_revision();
    SnapshotLock lock;
    s_snapshot = found ? cached : What2EatSnapshot{};
    s_last_sync_ms = 0;
    Serial.printf("[D][W2E] begin state=%u revision=%lu items=%u\n",
                  static_cast<unsigned>(s_snapshot.state),
                  static_cast<unsigned long>(s_snapshot.revision),
                  static_cast<unsigned>(s_snapshot.itemCount));
}

void dn_what2eat_tick() {
    const uint32_t now = millis();
    if ((s_last_sync_ms != 0 && now - s_last_sync_ms < kSyncIntervalMs) ||
        WiFi.status() != WL_CONNECTED) return;
    s_last_sync_ms = now;

    What2EatSnapshot& before = s_sync_before;
    before = {};
    {
        SnapshotLock lock;
        before = s_snapshot;
    }
    {
        SnapshotLock lock;
        s_snapshot.state = WHAT2EAT_SYNCING;
    }

    char suffix[80];
    snprintf(suffix, sizeof(suffix), "/api/what2eat/sync?after=%lu",
             static_cast<unsigned long>(before.revision));
    char url[224];
    if (!service_url(url, sizeof(url), suffix)) {
        mark_sync_failed();
        return;
    }

    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(url)) {
        mark_sync_failed();
        return;
    }
    const int code = http.GET();
    if (code == HTTP_CODE_NO_CONTENT) {
        http.end();
        restore_after_sync(before.state);
        retry_applied_ack(before.revision);
        return;
    }
    if (code != HTTP_CODE_OK) {
        http.end();
        mark_sync_failed();
        return;
    }
    const String payload = http.getString();
    http.end();
    if (payload.length() == 0 || payload.length() > kMaxSyncPayloadBytes) {
        mark_sync_failed();
        return;
    }

    What2EatSnapshot& candidate = s_sync_candidate;
    candidate = {};
    if (!dn_what2eat_parse_envelope(payload.c_str(), &candidate)) {
        uint32_t rejectedRevision = 0;
        json_uint(payload.c_str(), "revision", &rejectedRevision);
        mark_sync_failed();
        if (rejectedRevision > 0) ack_revision(rejectedRevision, "rejected", "invalid_payload");
        return;
    }
    if (!payload_hash_matches(payload.c_str(), candidate.contentHash)) {
        mark_sync_failed();
        ack_revision(candidate.revision, "rejected", "hash_mismatch");
        return;
    }

    if (!dn_what2eat_candidate_is_newer(before, candidate)) {
        restore_after_sync(before.state);
        retry_applied_ack(candidate.revision);
        return;
    }
    candidate.selectedIndex = dn_what2eat_pick_index(candidate.itemCount, -1, esp_random());
    if (!save_cache(candidate)) {
        mark_sync_failed();
        ack_revision(candidate.revision, "rejected", "cache_write_failed");
        return;
    }
    {
        SnapshotLock lock;
        s_snapshot = candidate;
        s_snapshot.state = WHAT2EAT_APPLIED;
    }
    retry_applied_ack(candidate.revision);
    Serial.printf("[D][W2E] applied revision=%lu items=%u\n",
                  static_cast<unsigned long>(candidate.revision),
                  static_cast<unsigned>(candidate.itemCount));
}

What2EatSnapshot dn_what2eat_snapshot() {
    What2EatSnapshot copy;
    const SemaphoreHandle_t mutex = s_mutex;
    if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(kSnapshotWaitMs)) != pdTRUE) {
        return copy;
    }
    copy = s_snapshot;
    xSemaphoreGive(mutex);
    return copy;
}

bool dn_what2eat_pick() {
    SnapshotLock lock;
    if (s_snapshot.itemCount == 0) return false;
    s_snapshot.selectedIndex = dn_what2eat_pick_index(
        s_snapshot.itemCount, s_snapshot.selectedIndex, esp_random());
    return s_snapshot.selectedIndex >= 0;
}

} // namespace desknest
