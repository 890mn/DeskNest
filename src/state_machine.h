// src/state_machine.h
// 栖屏 DeskNest - 4 轴状态机
// "栖于桌面，息于常亮之间"
//
// 4 个**正交**的状态字段 + 一个 update(g, b) 入口。
// 状态转移表见 plan §2.1 / §9.7。

#ifndef DESKNEST_STATE_MACHINE_H
#define DESKNEST_STATE_MACHINE_H

#include "config.h"
#include "gesture.h"

namespace desknest {

struct StateSnapshot {
    SystemState      system;
    OrientationState orientation;
    UIPage           page;
    RotationLock     rotLock;
    uint32_t         lastInputMs;

    // 进入特殊状态前的页面（face-down / landscape 切换时保留原页面，
    // 醒来或转回来时恢复，不打断用户正常导航）
    UIPage           pre_face_down_page = PAGE_PORTRAIT_OVERVIEW;
    UIPage           pre_landscape_page = PAGE_LANDSCAPE_OVERVIEW;
};

class StateMachine {
public:
    void begin();

    // 每帧：吃 gesture + button + 当前加速度
    void update(GestureEvent g, ButtonEvent b, OrientationState detected,
                uint32_t now_ms);

    // 立即查询
    const StateSnapshot& snapshot() const { return _s; }

    // 强控（SETTINGS / 工厂复位用）
    void forcePage(UIPage p)               { _s.page = p; }
    void forceSystem(SystemState s)        { _s.system = s; _s.lastInputMs = millis(); }
    void forceRotLock(RotationLock r)      { _s.rotLock = r; }
    void notifyInput()                     { _s.lastInputMs = millis(); }

    // 工具：竖/横屏页面循环
    static UIPage nextPortrait(UIPage p);
    static UIPage prevPortrait(UIPage p);
    static UIPage nextLandscape(UIPage p);
    static UIPage prevLandscape(UIPage p);

private:
    void applyGesture_(GestureEvent g, uint32_t now_ms);
    void applyButton_(ButtonEvent b, uint32_t now_ms);
    void applyOrientation_(OrientationState detected, uint32_t now_ms);
    void applyPowerTimeout_(uint32_t now_ms);

    StateSnapshot _s = {};
    uint32_t      _temp_unlock_expire_ms = 0;  // ROT_LOCKED_TEMP_5S
};

extern StateMachine g_state;

} // namespace desknest

#endif // DESKNEST_STATE_MACHINE_H
