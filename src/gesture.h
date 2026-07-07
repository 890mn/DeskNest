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

    OrientationState classifyOrientation_(float ax, float ay) const;
    bool detectShake_(float a_mag, uint32_t now_ms);
    bool detectTap_(const AccelReading& acc, uint32_t now_ms);
};

extern GestureEngine g_gesture;

} // namespace desknest

#endif // DESKNEST_GESTURE_H
