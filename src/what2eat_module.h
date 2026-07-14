#pragma once

#include <stddef.h>
#include <stdint.h>

namespace desknest {

// The wire/cache contract supports up to fifteen candidates. The board only
// renders rows that are present in the received envelope.
constexpr uint8_t WHAT2EAT_MAX_ITEMS = 15;
constexpr uint16_t WHAT2EAT_SCHEMA_VERSION = 1;
// score is encoded in tenths on the wire: 85 means 8.5.
constexpr uint8_t WHAT2EAT_SCORE_MIN_TENTHS = 10;
constexpr uint8_t WHAT2EAT_SCORE_MAX_TENTHS = 100;
constexpr uint8_t WHAT2EAT_SCORE_STEP_TENTHS = 5;

enum What2EatState : uint8_t {
    WHAT2EAT_ABSENT = 0,
    WHAT2EAT_CACHED,
    WHAT2EAT_SYNCING,
    WHAT2EAT_APPLIED,
    WHAT2EAT_FAILED_USING_CACHE,
};

struct What2EatItem {
    char id[24] = {};
    char name[32] = {};
    uint16_t count = 0;
    char price[10] = {};
    uint8_t score = 0;
};

struct What2EatSnapshot {
    What2EatState state = WHAT2EAT_ABSENT;
    uint16_t schemaVersion = WHAT2EAT_SCHEMA_VERSION;
    uint32_t revision = 0;
    char contentHash[65] = {};
    What2EatItem items[WHAT2EAT_MAX_ITEMS] = {};
    uint8_t itemCount = 0;
    int8_t selectedIndex = -1;
};

// Pure seams used by native tests and by the network/storage implementation.
bool dn_what2eat_parse_envelope(const char* json, What2EatSnapshot* out);
bool dn_what2eat_candidate_is_newer(const What2EatSnapshot& current,
                                    const What2EatSnapshot& candidate);
bool dn_what2eat_ack_needed(uint32_t cachedRevision, uint32_t ackedRevision);
int8_t dn_what2eat_pick_index(uint8_t itemCount, int8_t currentIndex,
                              uint32_t entropy);

// The owner task performs network and NVS mutation through begin/tick. UI and
// state-machine callers only receive or mutate a short-lock, deep-copy snapshot.
void dn_what2eat_begin();
void dn_what2eat_tick();
What2EatSnapshot dn_what2eat_snapshot();
bool dn_what2eat_pick();

} // namespace desknest
