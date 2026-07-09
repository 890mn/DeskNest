#include <unity.h>

#include "../../src/page_registry.h"

using namespace desknest;

void test_portrait_registry_reports_count_and_titles() {
    TEST_ASSERT_EQUAL_UINT8(5, dn_page_group_count(PAGE_GROUP_PORTRAIT));
    TEST_ASSERT_EQUAL_STRING("DeskNest", dn_page_title_from_registry(PAGE_PORTRAIT_OVERVIEW));
    TEST_ASSERT_EQUAL_STRING("AI Usage", dn_page_title_from_registry(PAGE_PORTRAIT_AI_USAGE));
    TEST_ASSERT_EQUAL_STRING("今天吃什么", dn_page_title_from_registry(PAGE_PORTRAIT_MENU));
    TEST_ASSERT_EQUAL_STRING("Environment", dn_page_title_from_registry(PAGE_PORTRAIT_ENVIRONMENT));
    TEST_ASSERT_EQUAL_STRING("Settings", dn_page_title_from_registry(PAGE_PORTRAIT_SETTINGS));
}

void test_portrait_registry_cycles_next_and_previous() {
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_AI_USAGE,
                      dn_next_page_in_group(PAGE_GROUP_PORTRAIT, PAGE_PORTRAIT_OVERVIEW));
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_MENU,
                      dn_next_page_in_group(PAGE_GROUP_PORTRAIT, PAGE_PORTRAIT_AI_USAGE));
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_ENVIRONMENT,
                      dn_next_page_in_group(PAGE_GROUP_PORTRAIT, PAGE_PORTRAIT_MENU));
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_OVERVIEW,
                      dn_next_page_in_group(PAGE_GROUP_PORTRAIT, PAGE_PORTRAIT_SETTINGS));

    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_SETTINGS,
                      dn_prev_page_in_group(PAGE_GROUP_PORTRAIT, PAGE_PORTRAIT_OVERVIEW));
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_ENVIRONMENT,
                      dn_prev_page_in_group(PAGE_GROUP_PORTRAIT, PAGE_PORTRAIT_SETTINGS));
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_MENU,
                      dn_prev_page_in_group(PAGE_GROUP_PORTRAIT, PAGE_PORTRAIT_ENVIRONMENT));
}

void test_registry_falls_back_to_first_page_for_foreign_page() {
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_OVERVIEW,
                      dn_next_page_in_group(PAGE_GROUP_PORTRAIT, PAGE_LANDSCAPE_FOCUS));
    TEST_ASSERT_EQUAL(PAGE_PORTRAIT_OVERVIEW,
                      dn_prev_page_in_group(PAGE_GROUP_PORTRAIT, PAGE_LANDSCAPE_FOCUS));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_portrait_registry_reports_count_and_titles);
    RUN_TEST(test_portrait_registry_cycles_next_and_previous);
    RUN_TEST(test_registry_falls_back_to_first_page_for_foreign_page);
    return UNITY_END();
}
