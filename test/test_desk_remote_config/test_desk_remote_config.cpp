#include <unity.h>

#include "../../src/desk_remote_config.h"

using namespace desknest;

void test_remote_menu_parser_maps_editor_payload() {
    const char* json = "{\"menu\":{\"today\":\"今天 测试\",\"yesterday\":\"昨天 测试\",\"items\":[{\"name\":\"米饭\",\"price\":\"20\",\"score\":88,\"active\":true}]}}";
    DeskRemoteConfig config;
    TEST_ASSERT_TRUE(dn_desk_config_parse(json, &config));
    TEST_ASSERT_TRUE(config.ready);
    TEST_ASSERT_EQUAL_STRING("今天 测试", config.today);
    TEST_ASSERT_EQUAL_STRING("米饭", config.items[0].name);
    TEST_ASSERT_EQUAL_UINT8(88, config.items[0].score);
    TEST_ASSERT_TRUE(config.items[0].active);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_remote_menu_parser_maps_editor_payload);
    return UNITY_END();
}
