// src/buttons.h
// 栖屏 DeskNest - 按键轮询（K10 BSP）
// "栖于桌面，息于常亮之间"
//
// 用 K10 BSP 的 k10.buttonA / buttonB / buttonAB ->isPressed()，
// 不直接操作 GPIO。引脚由 BSP 内部管（eP5_KeyA=12, eP11_KeyB=2）。
//
// 边沿检测 + 长按识别 —— 一帧返回一个 ButtonEvent。
// 短按阈值 1000ms，长按 1000ms（released 前若已 long 则不补发短按）；
// A+B 短按 BUTTON_SELECT，A+B 长按 3s BUTTON_FACTORY。

#ifndef DESKNEST_BUTTONS_H
#define DESKNEST_BUTTONS_H

#include <stdint.h>

// config.h 里的 ButtonEvent 在 namespace desknest 里；不包 extern "C"
#include "config.h"

namespace desknest {

struct ButtonPressTracker {
    bool prevPressed = false;
    uint32_t pressStartMs = 0;
    bool longFired = false;
};

// Pure edge/re-arm seam shared by the K10 polling adapter and native tests.
inline ButtonEvent dn_button_tick_press(ButtonPressTracker& tracker,
                                        bool pressed, uint32_t nowMs,
                                        uint32_t longPressMs,
                                        ButtonEvent shortEvent,
                                        ButtonEvent longEvent) {
    ButtonEvent event = BUTTON_NONE;
    const bool edgeDown = pressed && !tracker.prevPressed;
    const bool edgeUp = !pressed && tracker.prevPressed;
    if (edgeDown) {
        tracker.pressStartMs = nowMs;
        tracker.longFired = false;
    }
    const uint32_t heldMs = pressed ? nowMs - tracker.pressStartMs : 0;
    if (edgeUp && !tracker.longFired) event = shortEvent;
    if (pressed && !tracker.longFired && heldMs >= longPressMs) {
        tracker.longFired = true;
        event = longEvent;
    }
    tracker.prevPressed = pressed;
    return event;
}

inline void dn_button_suppress_combo_member(ButtonPressTracker& tracker,
                                            bool pressed, uint32_t nowMs) {
    tracker.prevPressed = pressed;
    tracker.pressStartMs = nowMs;
    tracker.longFired = pressed;
}

// Complete pure input arbitration seam. Synchronizing A/B before returning a
// combo event prevents their stale release edges from becoming ghost singles.
inline ButtonEvent dn_button_tick_inputs(ButtonPressTracker& a,
                                         ButtonPressTracker& b,
                                         ButtonPressTracker& combo,
                                         bool aPressed, bool bPressed,
                                         uint32_t nowMs,
                                         uint32_t singleLongMs = 1000,
                                         uint32_t comboLongMs = 3000) {
    const bool comboPressed = aPressed && bPressed;
    ButtonEvent event = dn_button_tick_press(combo, comboPressed, nowMs,
                                              comboLongMs, BUTTON_SELECT,
                                              BUTTON_FACTORY);
    if (event != BUTTON_NONE) {
        dn_button_suppress_combo_member(a, aPressed, nowMs);
        dn_button_suppress_combo_member(b, bPressed, nowMs);
        return event;
    }

    if (comboPressed) {
        a.prevPressed = aPressed;
        b.prevPressed = bPressed;
        return BUTTON_NONE;
    }

    event = dn_button_tick_press(a, aPressed, nowMs, singleLongMs,
                                 BUTTON_NEXT, BUTTON_MENU);
    if (event != BUTTON_NONE) return event;
    return dn_button_tick_press(b, bPressed, nowMs, singleLongMs,
                                BUTTON_PREV, BUTTON_BACK);
}

// 主循环每帧调用一次；返回本帧待处理的事件（NONE = 无）
ButtonEvent dn_button_poll(uint32_t now_ms);
bool dn_gesture_confirm_held();

}  // namespace desknest

#endif // DESKNEST_BUTTONS_H
