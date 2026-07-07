// src/gesture.cpp
// 栖屏 DeskNest - 手势识别实现
// "栖于桌面，息于常亮之间"

#include "gesture.h"

#include <Arduino.h>
#include <math.h>

namespace desknest {

GestureEngine g_gesture;

void GestureEngine::begin() {
    _orient.reset(ORIENTATION_PORTRAIT);  // 初始假设竖屏
    _pending = GESTURE_NONE;
    Serial.println("[D][GESTURE] engine ready");
}

OrientationState GestureEngine::classifyOrientation_(float ax, float ay) const {
    // 参考 plan §4.2：
    //   |ax|>0.7g ∧ |ay|<0.3g → LANDSCAPE
    //   |ay|>0.7g ∧ |ax|<0.3g → PORTRAIT
    const float G_R   = defaults::G_ROTATE_THRESHOLD;   // 0.7
    const float G_AMB = G_R - 0.4f;                     // 0.3 滞回

    if (fabsf(ax) > G_R && fabsf(ay) < G_AMB) {
        return (ax > 0) ? ORIENTATION_LANDSCAPE : ORIENTATION_LANDSCAPE;  // 暂不区分 left/right
    }
    if (fabsf(ay) > G_R && fabsf(ax) < G_AMB) {
        return ORIENTATION_PORTRAIT;
    }
    return _orient.value();  // 中间地带：保持当前
}

bool GestureEngine::detectShake_(float a_mag, uint32_t now_ms) {
    // 200ms 窗口内 |a| > 1.5g 触发
    if (now_ms - _peak_window_start_ms > 200) {
        _peak_abs_accel = 0;
        _peak_window_start_ms = now_ms;
    }
    if (a_mag > _peak_abs_accel) {
        _peak_abs_accel = a_mag;
    }
    if (_peak_abs_accel > defaults::G_SHAKE_THRESHOLD) {
        if (now_ms - _last_shake_ms >= defaults::T_SHAKE_COOLDOWN_MS) {
            _last_shake_ms = now_ms;
            _peak_abs_accel = 0;
            return true;
        }
    }
    return false;
}

bool GestureEngine::detectTap_(const AccelReading& acc, uint32_t now_ms) {
    // 简化：gz 在短时间内 spike > 1.2g 算 tap
    static float prev_gz = 1.0f;
    bool tap = (acc.z > 1.2f && prev_gz < 1.1f);
    prev_gz = acc.z;
    if (tap && (now_ms - _last_tap_ms) >= 300) {
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

    // 2) 翻面检测（Z > +0.7g 持续 800ms = 屏幕朝下/翻面栖息）
    if (az > defaults::G_FACE_DOWN_THRESHOLD) {
        // 累计时间
        static uint32_t face_down_since_ms = 0;
        if (face_down_since_ms == 0) {
            face_down_since_ms = now_ms;
        }
        if ((now_ms - face_down_since_ms) >= defaults::T_FACE_DOWN_STABLE_MS) {
            if (now_ms - _last_face_ms >= 2000) {
                _last_face_ms = now_ms;
                face_down_since_ms = 0;
                return GESTURE_FACE_DOWN;
            }
        }
    } else if (az < defaults::G_FACE_UP_THRESHOLD) {
        // 翻回
        static uint32_t face_up_since_ms = 0;
        if (face_up_since_ms == 0) {
            face_up_since_ms = now_ms;
        }
        if ((now_ms - face_up_since_ms) >= defaults::T_FACE_UP_STABLE_MS) {
            if (now_ms - _last_face_ms >= 2000) {
                _last_face_ms = now_ms;
                face_up_since_ms = 0;
                return GESTURE_FACE_UP_OPEN;
            }
        }
    } else {
        // 中性区：不重置 face_down/face_up 计时器 —— 它们各自的 if 分支
        // 会在 fire 时清零（line ~89、~103）。这里留空。
    }

    // 3) 旋转检测（X/Y 主轴，滞回 400ms）
    const OrientationState new_orient = classifyOrientation_(ax, ay);
    const OrientationState old_orient = _orient.value();

    if (new_orient != old_orient) {
        _orient.update(new_orient, now_ms, defaults::T_ROTATE_STABLE_MS);
        if (_orient.changed() && (now_ms - _last_rotate_ms) >= 1000) {
            _last_rotate_ms = now_ms;
            if (new_orient == ORIENTATION_LANDSCAPE) {
                return GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE;
            } else if (new_orient == ORIENTATION_PORTRAIT) {
                return GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT;
            }
        }
    } else {
        _orient.update(new_orient, now_ms, defaults::T_ROTATE_STABLE_MS);
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
