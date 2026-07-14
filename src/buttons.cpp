// src/buttons.cpp
// 栖屏 DeskNest - 按键轮询实现

#include "buttons.h"

#include <Arduino.h>
#include <unihiker_k10.h>

namespace desknest {

// 全局 K10 实例（extern 来自 sensors.cpp / app.cpp）
extern UNIHIKER_K10 k10;

namespace {

ButtonPressTracker g_a;
ButtonPressTracker g_b;
ButtonPressTracker g_ab;
bool g_gesture_confirm_held = false;

// 长按阈值（短按不需要阈值：按下→释放，期间未触发 long 即为短按）
constexpr uint32_t LONG_PRESS_MS     = 1000;   // 单键长按
constexpr uint32_t AB_LONG_PRESS_MS  = 3000;   // A+B 长按 = factory reset

}  // namespace

ButtonEvent dn_button_poll(uint32_t now_ms) {
    // 优先报告：长按 > A+B > 单键（A 优先于 B，因为 A 事件更具体）
    const bool a_now = k10.buttonA->isPressed();
    const bool b_now = k10.buttonB->isPressed();
#if DESKNEST_GESTURE_CONFIRM_BUTTON == GESTURE_CONFIRM_B
    g_gesture_confirm_held = b_now;
#elif DESKNEST_GESTURE_CONFIRM_BUTTON == GESTURE_CONFIRM_NONE
    g_gesture_confirm_held = true;
#else
    g_gesture_confirm_held = a_now;
#endif

    return dn_button_tick_inputs(g_a, g_b, g_ab, a_now, b_now, now_ms,
                                 LONG_PRESS_MS, AB_LONG_PRESS_MS);
}

bool dn_gesture_confirm_held() { return g_gesture_confirm_held; }

}  // namespace desknest
