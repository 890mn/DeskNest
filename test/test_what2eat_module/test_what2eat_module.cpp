#include <unity.h>

#include "../../src/what2eat_core.cpp"

#include <cstdio>
#include <cstring>

using namespace desknest;

namespace {

constexpr const char* kHash =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void makeEnvelope(char* out, size_t cap, uint32_t revision, const char* items) {
    std::snprintf(out, cap,
        "{\"schemaVersion\":1,\"revision\":%lu,\"contentHash\":\"%s\","
        "\"what2eat\":{\"items\":[%s]}}",
        static_cast<unsigned long>(revision), kHash, items);
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_valid_envelope_maps_count_without_selection() {
    char json[768];
    makeEnvelope(json, sizeof(json), 7,
        "{\"id\":\"noodle\",\"name\":\"番茄牛腩面\",\"count\":3,\"price\":\"28\",\"score\":85},"
        "{\"id\":\"rice\",\"name\":\"咖喱饭\",\"count\":5,\"price\":\"22\",\"score\":90}");
    What2EatSnapshot snapshot;
    TEST_ASSERT_TRUE(dn_what2eat_parse_envelope(json, &snapshot));
    TEST_ASSERT_EQUAL_UINT32(7, snapshot.revision);
    TEST_ASSERT_EQUAL_UINT8(2, snapshot.itemCount);
    TEST_ASSERT_EQUAL_INT8(-1, snapshot.selectedIndex);
    TEST_ASSERT_EQUAL_UINT16(3, snapshot.items[0].count);
    TEST_ASSERT_EQUAL_STRING("28", snapshot.items[0].price);
}

void test_invalid_payload_never_replaces_output() {
    What2EatSnapshot output;
    output.revision = 99;
    const char* bad =
        "{\"schemaVersion\":1,\"revision\":8,\"contentHash\":\"short\","
        "\"what2eat\":{\"items\":[]}}";
    TEST_ASSERT_FALSE(dn_what2eat_parse_envelope(bad, &output));
    TEST_ASSERT_EQUAL_UINT32(99, output.revision);
}

void test_rejects_duplicate_ids_empty_names_and_more_than_fifteen_items() {
    char json[4096];
    makeEnvelope(json, sizeof(json), 8,
        "{\"id\":\"same\",\"name\":\"A\",\"count\":1,\"price\":\"1\",\"score\":85},"
        "{\"id\":\"same\",\"name\":\"B\",\"count\":1,\"price\":\"1\",\"score\":85}");
    What2EatSnapshot snapshot;
    TEST_ASSERT_FALSE(dn_what2eat_parse_envelope(json, &snapshot));

    makeEnvelope(json, sizeof(json), 8,
        "{\"id\":\"empty\",\"name\":\"\",\"count\":1,\"price\":\"1\",\"score\":85}");
    TEST_ASSERT_FALSE(dn_what2eat_parse_envelope(json, &snapshot));

    char manyItems[4096] = {};
    size_t used = 0;
    for (int i = 0; i < 15; ++i) {
        const int written = std::snprintf(manyItems + used, sizeof(manyItems) - used,
            "%s{\"id\":\"item-%d\",\"name\":\"菜%d\",\"count\":1,\"price\":\"1\",\"score\":85}",
            i == 0 ? "" : ",", i, i);
        TEST_ASSERT_TRUE(written > 0);
        used += static_cast<size_t>(written);
        TEST_ASSERT_TRUE(used < sizeof(manyItems));
    }
    makeEnvelope(json, sizeof(json), 8, manyItems);
    TEST_ASSERT_TRUE(dn_what2eat_parse_envelope(json, &snapshot));
    TEST_ASSERT_EQUAL_UINT8(15, snapshot.itemCount);

    const size_t fifteenLength = used;
    const int written = std::snprintf(manyItems + used, sizeof(manyItems) - used,
        ",\"id\":\"item-15\",\"name\":\"菜15\",\"count\":1,\"price\":\"1\",\"score\":85}");
    TEST_ASSERT_TRUE(written > 0);
    used += static_cast<size_t>(written);
    TEST_ASSERT_TRUE(used > fifteenLength);
    makeEnvelope(json, sizeof(json), 8, manyItems);
    TEST_ASSERT_FALSE(dn_what2eat_parse_envelope(json, &snapshot));
}

void test_revision_is_idempotent_and_pick_changes_without_count_mutation() {
    What2EatSnapshot current;
    current.revision = 10;
    current.itemCount = 2;
    current.items[0].count = 4;
    current.items[1].count = 9;
    What2EatSnapshot same = current;
    What2EatSnapshot newer = current;
    newer.revision = 11;
    TEST_ASSERT_FALSE(dn_what2eat_candidate_is_newer(current, same));
    TEST_ASSERT_TRUE(dn_what2eat_candidate_is_newer(current, newer));
    TEST_ASSERT_TRUE(dn_what2eat_ack_needed(10, 9));
    TEST_ASSERT_FALSE(dn_what2eat_ack_needed(10, 10));
    TEST_ASSERT_FALSE(dn_what2eat_ack_needed(0, 0));

    const int8_t selected = dn_what2eat_pick_index(2, 0, 0);
    TEST_ASSERT_EQUAL_INT8(1, selected);
    TEST_ASSERT_EQUAL_UINT16(4, current.items[0].count);
    TEST_ASSERT_EQUAL_UINT16(9, current.items[1].count);
    TEST_ASSERT_EQUAL_INT8(-1, dn_what2eat_pick_index(0, -1, 5));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_envelope_maps_count_without_selection);
    RUN_TEST(test_invalid_payload_never_replaces_output);
    RUN_TEST(test_rejects_duplicate_ids_empty_names_and_more_than_fifteen_items);
    RUN_TEST(test_revision_is_idempotent_and_pick_changes_without_count_mutation);
    return UNITY_END();
}
