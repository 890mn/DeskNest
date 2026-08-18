// DeskNest - component test page data and hardware entry points

#ifndef DESKNEST_COMPONENT_TEST_MODULE_H
#define DESKNEST_COMPONENT_TEST_MODULE_H

#include <stdint.h>

namespace desknest {

constexpr uint8_t COMPONENT_TEST_MAX_ROWS = 3;

struct ComponentTestRow {
    char name[24] = {};
    char execution[16] = {};
    char content[48] = {};
    char result[32] = {};
};

struct ComponentTestSnapshot {
    ComponentTestRow rows[COMPONENT_TEST_MAX_ROWS];
    uint8_t rowCount = 0;
    uint8_t selectedIndex = 0;
};

// Hardware-facing lifecycle.  The calls are intentionally non-blocking from
// the main loop; only the selected test owns an active sequence at a time.
void dn_component_test_begin();
void dn_component_test_tick(uint32_t now_ms);
void dn_component_test_select_next();
void dn_component_test_execute();
void dn_component_test_abort();
bool dn_component_test_is_running();
ComponentTestSnapshot dn_component_test_snapshot();

} // namespace desknest

#endif // DESKNEST_COMPONENT_TEST_MODULE_H
