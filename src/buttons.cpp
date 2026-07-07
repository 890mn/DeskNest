// src/buttons.cpp
// 栖屏 DeskNest - 按键轮询实现

#include "buttons.h"

#include <Arduino.h>
#include <unihiker_k10.h>

namespace desknest {

// 全局 K10 实例（extern 来自 sensors.cpp / app.cpp）
extern UNIHIKER_K10 k10;

namespace {

// 单按键跟踪器：边沿检测 + 长按一帧一次
struct Tracker {
    bool     prev_pressed = false;
    uint32_t press_start_ms = 0;
    bool     long_fired = false;
};

Tracker g_a;
Tracker g_b;
Tracker g_ab;

// 长按阈值（短按不需要阈值：按下→释放，期间未触发 long 即为短按）
constexpr uint32_t LONG_PRESS_MS     = 1000;   // 单键长按
constexpr uint32_t AB_LONG_PRESS_MS  = 3000;   // A+B 长按 = factory reset

// 跟踪单个按键：本帧是否有事件 + 更新内部状态
//   返回事件（若有），状态在函数内部已更新
//
// 关键 bug 修复：必须先更新 press_start_ms，再算 held_ms。
// 否则第一帧按下时 held_ms = now_ms - 0 = now_ms（巨大），长按立刻误触发。
ButtonEvent tick_one(Tracker& t, bool now_pressed, uint32_t now_ms,
                     uint32_t long_ms,
                     ButtonEvent short_event, ButtonEvent long_event) {
    ButtonEvent e = BUTTON_NONE;
    const bool edge_down = now_pressed && !t.prev_pressed;
    const bool edge_up   = !now_pressed && t.prev_pressed;

    // 1) 先处理 edge_down —— 把 press_start_ms 设到 now
    if (edge_down) {
        t.press_start_ms = now_ms;
        t.long_fired = false;
    }

    // 2) 再算 held_ms（此时 press_start_ms 已是新的）
    const uint32_t held_ms = now_pressed ? (now_ms - t.press_start_ms) : 0;

    // 3) edge_up 时若 long 没 fire 过 → 短按
    if (edge_up && !t.long_fired) {
        e = short_event;
    }

    // 4) 按住超过 long_ms（一次性）
    if (now_pressed && !t.long_fired && held_ms >= long_ms) {
        t.long_fired = true;
        e = long_event;
    }

    t.prev_pressed = now_pressed;
    return e;
}

}  // namespace

ButtonEvent dn_button_poll(uint32_t now_ms) {
    // 优先报告：长按 > A+B > 单键（A 优先于 B，因为 A 事件更具体）
    ButtonEvent e = BUTTON_NONE;

    const bool a_now = k10.buttonA->isPressed();
    const bool b_now = k10.buttonB->isPressed();
    const bool ab_now = a_now && b_now;

    // A+B 组合（独立 tracker）
    e = tick_one(g_ab, ab_now, now_ms, AB_LONG_PRESS_MS,
                 BUTTON_SELECT, BUTTON_FACTORY);
    if (e != BUTTON_NONE) return e;

    // A 单独（且不在 A+B 时 —— 否则 ab 已处理）
    if (!ab_now) {
        e = tick_one(g_a, a_now, now_ms, LONG_PRESS_MS,
                     BUTTON_NEXT, BUTTON_MENU);
        if (e != BUTTON_NONE) return e;
    } else {
        // A 被 B 盖住时不要触发 A 的长按；保持 tracker 同步但不 fire
        g_a.prev_pressed = a_now;
        if (!a_now) g_a.long_fired = false;
    }

    // B 单独
    if (!ab_now) {
        e = tick_one(g_b, b_now, now_ms, LONG_PRESS_MS,
                     BUTTON_PREV, BUTTON_BACK);
        if (e != BUTTON_NONE) return e;
    } else {
        g_b.prev_pressed = b_now;
        if (!b_now) g_b.long_fired = false;
    }

    return e;
}

}  // namespace desknest