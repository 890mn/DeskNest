// src/state_machine.h
// 栖屏 DeskNest - 4 轴状态机
// "栖于桌面，息于常亮之间"
//
// 4 个**正交**的状态字段 + 多输入源独立入口。
// 状态转移表见 plan §2.1 / §9.7。
//
// 输入源在编译期通过宏 ENABLE_*_INPUT 单独开关 —— 同一时间可以打开多个，
// 每个入口都从当前快照出发独立处理，不共享中间状态。

#ifndef DESKNEST_STATE_MACHINE_H
#define DESKNEST_STATE_MACHINE_H

#include "config.h"
#include "gesture.h"

namespace desknest {

// ---------------------------------------------------------------------------
// 输入源编译期开关（默认全开；要屏蔽某路输入就在 platformio.ini 里
// -DENABLE_BUTTON_INPUT=0 等覆盖）
// ---------------------------------------------------------------------------
#ifndef ENABLE_FACE_INPUT
#define ENABLE_FACE_INPUT         1
#endif
#ifndef ENABLE_GESTURE_INPUT
#define ENABLE_GESTURE_INPUT      1
#endif
#ifndef ENABLE_BUTTON_INPUT
#define ENABLE_BUTTON_INPUT       1
#endif
#ifndef ENABLE_ORIENTATION_INPUT
#define ENABLE_ORIENTATION_INPUT  1
#endif
#ifndef ENABLE_POWER_TIMEOUT
#define ENABLE_POWER_TIMEOUT      1
#endif

struct StateSnapshot {
    SystemState      system;
    FaceSubState     face_state = FACE_STATE_UP;   // 显式翻面子状态
    OrientationState orientation;
    UIPage           page;
    RotationLock     rotLock;
    uint32_t         lastInputMs;

    // 记忆模式：每种姿态记下自己的"最后页面"，切回去时恢复
    //   pre_face_down_page —— 翻面前的页面（face-up 时恢复）
    //   last_portrait_page —— 上次在 portrait 时的页面（L→P 旋转回竖屏时恢复）
    //   last_landscape_page —— 上次在 landscape 时的页面（P→L 旋转到横屏时恢复）
    UIPage           pre_face_down_page   = PAGE_PORTRAIT_OVERVIEW;
    UIPage           last_portrait_page   = PAGE_PORTRAIT_OVERVIEW;
    UIPage           last_landscape_page  = PAGE_LANDSCAPE_FOCUS;
};

class StateMachine {
public:
    void begin();

#if ENABLE_FACE_INPUT
    // 翻面 / 翻回 —— 优先级最高的入口
    // face = GESTURE_FACE_DOWN 或 GESTURE_FACE_UP_OPEN，其它值被忽略
    void updateFace(GestureEvent face, uint32_t now_ms);
#endif

#if ENABLE_GESTURE_INPUT
    // 手势输入（摇动 / 旋转等）—— 进入 page-level 导航
    void updateGesture(GestureEvent g, uint32_t now_ms);
#endif

#if ENABLE_BUTTON_INPUT
    // 物理按键输入 —— 与手势在同一优先级，独立处理
    void updateButton(ButtonEvent b, uint32_t now_ms);
#endif

#if ENABLE_ORIENTATION_INPUT
    // 姿态同步入口。
    // 当前 MVP 不做运行时横竖屏动态切换；LANDSCAPE 检测不改变 page。
    // 横屏只作为后期 boot-time display orientation adapter 保留。
    void updateOrientation(OrientationState detected, uint32_t now_ms);
#endif

#if ENABLE_POWER_TIMEOUT
    // 电源档位超时（30s/90s 进 AMBIENT/LIGHT_SLEEP）—— 周期性 tick
    void tickPowerTimeout(uint32_t now_ms);
#endif

    // 兼容旧接口：单次 update() = 按优先级依次调各个 update*()
    // 实际生产代码现在应该用各个独立入口
    void update(GestureEvent g, ButtonEvent b, OrientationState detected,
                uint32_t now_ms);

    // 立即查询
    const StateSnapshot& snapshot() const { return _s; }
    // Current navigation safety gate, exposed read-only for the UI chrome.
    bool gestureConfirmEnabled() const;

    // 强控（SETTINGS / 工厂复位用）
    void forcePage(UIPage p)               { _s.page = p; }
    void forceSystem(SystemState s)        { _s.system = s; _s.lastInputMs = millis(); }
    void forceRotLock(RotationLock r)      { _s.rotLock = r; }
    void notifyInput()                     { _s.lastInputMs = millis(); }

    // 工具：页面循环。
    // 当前运行路径只使用 portrait；landscape helpers 为后期启动横屏适配保留。
    static UIPage nextPortrait(UIPage p);
    static UIPage prevPortrait(UIPage p);
    static UIPage nextLandscape(UIPage p);
    static UIPage prevLandscape(UIPage p);

private:
    // 内部：每路输入独立的 handler —— 不共享中间状态，每帧从 IDLE 出发
    void applyFace_(GestureEvent face, uint32_t now_ms);
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
