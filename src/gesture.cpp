// src/gesture.cpp
// 栖屏 DeskNest - 手势识别实现
// "栖于桌面，息于常亮之间"

#include "gesture.h"

#include <Arduino.h>
#include <math.h>

namespace desknest {

GestureEngine g_gesture;

// ---------------------------------------------------------------------------
// 运行时可调阈值 —— 初始值 = config.h::defaults；
// 几个原来散落在 gesture.cpp 各处的硬编码（1000/2000/300/200/1.2/1.1）
// 也都搬到这里，由 src/gesture_tuning.cpp 的串口 REPL 改。
// ---------------------------------------------------------------------------
GestureTuning g_tuning = {
    // 翻面
    .face_down_threshold = defaults::G_FACE_DOWN_THRESHOLD,   // +0.7
    .face_up_threshold   = defaults::G_FACE_UP_THRESHOLD,     // -0.7
    .face_down_stable_ms = defaults::T_FACE_DOWN_STABLE_MS,   // 800
    .face_up_stable_ms   = defaults::T_FACE_UP_STABLE_MS,     // 300
    .face_cooldown_ms    = 2000,
    // 旋转
    .rotate_threshold    = defaults::G_ROTATE_THRESHOLD,      // 0.7
    .rotate_amb          = 0.4f,                              // 0.3 = 0.7 - 0.4
    .rotate_stable_ms    = defaults::T_ROTATE_STABLE_MS,      // 400
    .rotate_cooldown_ms  = 1000,
    // 摇动
    .shake_threshold     = defaults::G_SHAKE_THRESHOLD,       // 1.5
    .shake_window_ms     = 200,
    .shake_cooldown_ms   = defaults::T_SHAKE_COOLDOWN_MS,    // 600
    // Tap
    .tap_z_high          = 1.2f,
    .tap_z_low           = 1.1f,
    .tap_cooldown_ms     = 300,
};

void GestureEngine::begin() {
    _orient.reset(ORIENTATION_PORTRAIT);  // 初始假设竖屏
    _pending = GESTURE_NONE;
    Serial.println("[D][GESTURE] engine ready");
}

OrientationState GestureEngine::classifyOrientation_(float ax, float ay) const {
    // 参考 plan §4.2：
    //   |ax|>rotate_threshold ∧ |ay|<rotate_threshold-rotate_amb → LANDSCAPE
    //   |ay|>rotate_threshold ∧ |ax|<rotate_threshold-rotate_amb → PORTRAIT
    const float G_R   = g_tuning.rotate_threshold;
    const float G_AMB = G_R - g_tuning.rotate_amb;

    if (fabsf(ax) > G_R && fabsf(ay) < G_AMB) {
        return (ax > 0) ? ORIENTATION_LANDSCAPE : ORIENTATION_LANDSCAPE;  // 暂不区分 left/right
    }
    if (fabsf(ay) > G_R && fabsf(ax) < G_AMB) {
        return ORIENTATION_PORTRAIT;
    }
    return _orient.value();  // 中间地带：保持当前
}

bool GestureEngine::detectShake_(float a_mag, uint32_t now_ms) {
    // shake_window_ms 窗口内 |a| > shake_threshold 触发
    if (now_ms - _peak_window_start_ms > g_tuning.shake_window_ms) {
        _peak_abs_accel = 0;
        _peak_window_start_ms = now_ms;
    }
    if (a_mag > _peak_abs_accel) {
        _peak_abs_accel = a_mag;
    }
    if (_peak_abs_accel > g_tuning.shake_threshold) {
        if (now_ms - _last_shake_ms >= g_tuning.shake_cooldown_ms) {
            _last_shake_ms = now_ms;
            _peak_abs_accel = 0;
            return true;
        }
    }
    return false;
}

bool GestureEngine::detectTap_(const AccelReading& acc, uint32_t now_ms) {
    // 简化：gz 在短时间内 spike > tap_z_high 算 tap（prev < tap_z_low）
    static float prev_gz = 1.0f;
    bool tap = (acc.z > g_tuning.tap_z_high && prev_gz < g_tuning.tap_z_low);
    prev_gz = acc.z;
    if (tap && (now_ms - _last_tap_ms) >= g_tuning.tap_cooldown_ms) {
        _last_tap_ms = now_ms;
        return true;
    }
    return false;
}

GestureEvent GestureEngine::update(const AccelReading& acc, uint32_t now_ms) {
    if (!acc.valid) return GESTURE_NONE;

    // 1) 滑动平均
    _wx.push(acc.x);
    _wy.push(acc.y);
    _wz.push(acc.z);

    const float ax = _wx.avg();
    const float ay = _wy.avg();
    const float az = _wz.avg();
    const float a_mag = sqrtf(ax*ax + ay*ay + az*az);

    // 2) 翻面检测（az > face_down_threshold 持续 face_down_stable_ms）
    if (az > g_tuning.face_down_threshold) {
        // 累计时间
        static uint32_t face_down_since_ms = 0;
        if (face_down_since_ms == 0) {
            face_down_since_ms = now_ms;
        }
        if ((now_ms - face_down_since_ms) >= g_tuning.face_down_stable_ms) {
            if (now_ms - _last_face_ms >= g_tuning.face_cooldown_ms) {
                _last_face_ms = now_ms;
                face_down_since_ms = 0;
                return GESTURE_FACE_DOWN;
            }
        }
    } else if (az < g_tuning.face_up_threshold) {
        // 翻回
        static uint32_t face_up_since_ms = 0;
        if (face_up_since_ms == 0) {
            face_up_since_ms = now_ms;
        }
        if ((now_ms - face_up_since_ms) >= g_tuning.face_up_stable_ms) {
            if (now_ms - _last_face_ms >= g_tuning.face_cooldown_ms) {
                _last_face_ms = now_ms;
                face_up_since_ms = 0;
                return GESTURE_FACE_UP_OPEN;
            }
        }
    } else {
        // 中性区：不重置 face_down/face_up 计时器 —— 它们各自的 if 分支
        // 会在 fire 时清零（line ~89、~103）。这里留空。
    }

    // 3) 旋转检测（X/Y 主轴，滞回 rotate_stable_ms）
    const OrientationState new_orient = classifyOrientation_(ax, ay);
    const OrientationState old_orient = _orient.value();

    if (new_orient != old_orient) {
        _orient.update(new_orient, now_ms, g_tuning.rotate_stable_ms);
        if (_orient.changed() && (now_ms - _last_rotate_ms) >= g_tuning.rotate_cooldown_ms) {
            _last_rotate_ms = now_ms;
            if (new_orient == ORIENTATION_LANDSCAPE) {
                return GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE;
            } else if (new_orient == ORIENTATION_PORTRAIT) {
                return GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT;
            }
        }
    } else {
        _orient.update(new_orient, now_ms, g_tuning.rotate_stable_ms);
    }

    // 4) 摇动检测
    if (detectShake_(a_mag, now_ms)) {
        // 摇动方向：从 x 轴正负判断
        return (ax > 0) ? GESTURE_SHAKE_LEFT : GESTURE_SHAKE_RIGHT;
    }

    // 5) Tap
    if (detectTap_(acc, now_ms)) {
        return GESTURE_TAP;
    }

    return GESTURE_NONE;
}

} // namespace desknest
