#define DESKNEST_TEST_PAGE_ONLY 1

#include <unity.h>

#include "../../src/page_registry.h"

using namespace desknest;

void test_component_test_only_mode_exposes_one_portrait_page() {
    TEST_ASSERT_EQUAL_UINT8(1, dn_page_group_count(PAGE_GROUP_PORTRAIT));
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_COMPONENT_TEST,
                      dn_first_page_in_group(PAGE_GROUP_PORTRAIT));
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_COMPONENT_TEST,
                      dn_next_page_in_group(PAGE_GROUP_PORTRAIT,
                                             PAGE_PORTRAIT_COMPONENT_TEST));
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_COMPONENT_TEST,
                      dn_prev_page_in_group(PAGE_GROUP_PORTRAIT,
                                             PAGE_PORTRAIT_COMPONENT_TEST));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_component_test_only_mode_exposes_one_portrait_page);
    return UNITY_END();
}
