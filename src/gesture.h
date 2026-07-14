// src/gesture.h
// 栖屏 DeskNest - 手势识别
// "栖于桌面，息于常亮之间"
//
// 从 SC7A20H 三轴加速度原始数据 → 抽象的 GestureEvent。
// 内部用 0.5s 滑动平均 + 滞回 + 冷却时间，参考 plan §4.2。

#ifndef DESKNEST_GESTURE_H
#define DESKNEST_GESTURE_H

#include "config.h"
#include "sensors.h"

#include <math.h>  // 供 sqrtf / fabsf 使用（gesture.cpp 依赖）

namespace desknest {

// ============================================================================
// 内部用：滑动平均
// ============================================================================

template <typename T, uint8_t N>
class SlidingWindow {
public:
    SlidingWindow() : _idx(0), _filled(0), _sum(T{}) {}

    void push(T v) {
        if (_filled < N) {
            _buf[_idx] = v;
            _sum += v;
            _filled++;
        } else {
            _sum -= _buf[_idx];
            _buf[_idx] = v;
            _sum += v;
        }
        _idx = (_idx + 1) % N;
    }

    T avg() const {
        return _filled ? T(_sum / _filled) : T{};
    }

    uint8_t size() const { return _filled; }

    void reset() { _idx = 0; _filled = 0; _sum = T{}; }

private:
    T _buf[N];
    uint8_t _idx;
    uint8_t _filled;
    T _sum;
};

// ============================================================================
// 内部用：滞回状态机
// ============================================================================

template <typename T>
class Hysteresis {
public:
    Hysteresis(T initial = T{}) : _val(initial), _candidate(initial),
                                  _candidate_since_ms(0), _changed(false) {}

    // 每次读数调用 update。stable_ms 期间 candidate 不变才 commit。
    void update(T candidate, uint32_t now_ms, uint32_t stable_ms) {
        if (candidate != _candidate) {
            _candidate = candidate;
            _candidate_since_ms = now_ms;
            _changed = false;
        } else if (candidate != _val && (now_ms - _candidate_since_ms) >= stable_ms) {
            _val = candidate;
            _changed = true;
        } else {
            _changed = false;
        }
    }

    T value() const { return _val; }
    bool changed() const { return _changed; }
    void reset(T v = T{}) { _val = _candidate = v; _candidate_since_ms = 0; _changed = false; }

private:
    T _val;
    T _candidate;
    uint32_t _candidate_since_ms;
    bool _changed;
};

// ============================================================================
// 运行时可调阈值（被 src/gesture_tuning.cpp 的串口 REPL 修改）
// 默认值来自 config.h::defaults；TAP/COOLDOWN 几个原来是硬编码的也提到这里。
// ============================================================================

struct GestureTuning {
    // 翻面
    float    face_down_threshold;     // az > 此值稳定后进入 face-down
    float    face_up_threshold;       // az < 此值稳定后离开 face-down（字段名兼容 REPL）
    uint16_t face_down_stable_ms;     // 持续时间（默认 800ms）
    uint16_t face_up_stable_ms;       // 持续时间（默认 300ms）
    uint16_t face_cooldown_ms;        // 两次进入 face-down 的最小间隔（默认 2000ms）

    // 旋转
    float    rotate_threshold;        // 主轴 > 此值 ∧ 副轴 < rotate_threshold - rotate_amb
    float    rotate_amb;              // 副轴滞回宽度（默认 0.4g）
    uint16_t rotate_stable_ms;        // Hysteresis commit 窗口（默认 400ms）
    uint16_t rotate_cooldown_ms;      // 两次旋转事件最小间隔（默认 1000ms）

    // 摇动
    float    shake_threshold;         // 200ms 窗口内 |a| 峰值 > 此值（默认 1.5g）
    float    shake_return_threshold;  // 相对基线的反向峰值门槛
    float    shake_settle_threshold;  // 相对基线的回稳门槛
    uint16_t shake_window_ms;         // 摇动峰值跟踪窗口（默认 200ms）
    uint16_t shake_cooldown_ms;       // 两次摇动最小间隔（默认 600ms）
    uint8_t  shake_invert;            // 方向反转（K10 BSP 坐标方向不一致时用）
    uint8_t  shake_fire_on_outbound;  // 1 = outbound 触发；触发后仍需回中立才重新武装

    // Tap
    float    tap_z_high;              // 当前 az > 此值（默认 1.2g）
    float    tap_z_low;               // 上一拍 az < 此值（默认 1.1g）
    uint16_t tap_cooldown_ms;         // 两次 tap 最小间隔（默认 300ms）

    // 摇动手势屏蔽
    uint8_t  gesture_shake_enabled;   // 0 = 屏蔽（默认；用 A/B 代替），
                                       // 1 = 允许物理摇动（wizard 调参时自动开）

    // 输出（不属于手势识别，但 REPL 用来切 app.cpp 的 1Hz 心跳流）
    uint8_t  verbose;                 // 0 = 安静；1 = 1Hz 心跳 + 传感器都打
};

enum ShakePhase : uint8_t {
    SHAKE_PHASE_IDLE = 0,
    SHAKE_PHASE_OUTBOUND,
    SHAKE_PHASE_RETURNING,
    SHAKE_PHASE_WAIT_NEUTRAL,
};

inline uint8_t shakeAnimationPercent(ShakePhase phase) {
    switch (phase) {
        case SHAKE_PHASE_OUTBOUND:  return 35;
        case SHAKE_PHASE_RETURNING: return 70;
        default:                    return 0;
    }
}

inline float recommendShakeThreshold(float measured_peak) {
    float threshold = measured_peak * 0.65f;
    if (threshold < 0.35f) threshold = 0.35f;
    if (threshold > 0.90f) threshold = 0.90f;
    return threshold;
}

// 全局实例：默认值在 gesture.cpp 里填。REPL 的 'set' / 'reset' 改这个。
extern GestureTuning g_tuning;

// ============================================================================
// GestureEngine —— 把加速度原始数据转 GestureEvent
// ============================================================================

class GestureEngine {
public:
    void begin();
    GestureEvent update(const AccelReading& acc, uint32_t now_ms);

    // 用于测试 / 调试：注入一个事件
    void inject(GestureEvent e) { _pending = e; }

    // 内部状态访问（state_machine 也会用 orientation）
    OrientationState orientation() const { return _orient.value(); }
    ShakePhase shakePhase() const { return _shake_phase; }
    int8_t shakeDirection() const { return _shake_direction; } // +1=left, -1=right, 0=none
    float shakeBaselineAx() const { return _shake_baseline_ax; }
    float shakeMotionAx(float raw_ax) const { return raw_ax - _shake_baseline_ax; }

private:
    // 滑动平均窗口
    SlidingWindow<float, 8>  _wx, _wy, _wz;  // ≈ 0.27s @ 30Hz

    // 姿态滞回
    Hysteresis<OrientationState> _orient;

    // 上次输出事件
    GestureEvent _pending = GESTURE_NONE;

    // 旋转 / 翻面 / 摇动 上次触发时间（用于冷却）
    uint32_t _last_rotate_ms   = 0;
    uint32_t _last_face_ms     = 0;
    uint32_t _last_shake_ms    = 0;
    uint32_t _last_tap_ms      = 0;

    // 摇动：峰值跟踪
    float _peak_abs_accel = 0;
    uint32_t _peak_window_start_ms = 0;

    // 摇动检测：静止 → 首次峰值 → 反向峰值 → 连续回稳，或单次触发后等待回中立
    ShakePhase _shake_phase = SHAKE_PHASE_IDLE;
    int8_t     _shake_axis_sign = 0;   // 原始 ax 首峰符号
    int8_t     _shake_direction = 0;   // UI 语义：+1=left, -1=right
    uint8_t    _shake_settle_samples = 0;
    uint8_t    _shake_outbound_samples = 0;
    uint32_t   _shake_started_ms = 0;
    float      _shake_baseline_ax = 0.0f;
    bool       _shake_baseline_valid = false;

    // 翻面 / Tap：跨 update() 持续累加的"何时进入 zone"计时器
    //   之前是 update() 里的 static local，跨实例/跨 begin() 也会泄露；
    //   提为成员后 begin() 可以一并重置，校准 wizard 每步干净起步。
    uint32_t _face_down_since_ms = 0;
    uint32_t _face_up_since_ms   = 0;
    // Face transitions are edge-triggered: held face-down and held release
    // poses must not repeat. A release can only occur after face-down became active.
    bool     _face_down_armed    = true;
    bool     _face_up_armed      = false;
    bool     _face_down_active   = false;
    float    _tap_prev_gz        = 1.0f;

    OrientationState classifyOrientation_(float ax, float ay) const;
    // 摇动用 raw ax（不用滑动平均）—— 平滑会把反复过零的摇动信号压平
    GestureEvent detectShake_(float raw_ax, uint32_t now_ms);
    bool detectTap_(const AccelReading& acc, uint32_t now_ms);
};

extern GestureEngine g_gesture;

} // namespace desknest

#endif // DESKNEST_GESTURE_H
