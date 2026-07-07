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
    .shake_threshold     = 0.55f,                             // 自然手持甩动首峰
    .shake_return_threshold = 0.35f,                          // 反向回拉峰值
    .shake_settle_threshold = 0.16f,                          // 连续 3 帧进入此区间才提交
    .shake_window_ms     = 650,                               // 容纳甩出 + 回拉 + 回稳
    .shake_cooldown_ms   = 450,                               // 完整动作已回稳，可稍短
    .shake_invert        = 0,                                 // 0 = 约定 ax>0 → LEFT；1 = 反转
    // Tap
    .tap_z_high          = 1.2f,
    .tap_z_low           = 1.1f,
    .tap_cooldown_ms     = 300,

    // 输出
    .verbose             = 0,             // 默认安静
};

void GestureEngine::begin() {
    _orient.reset(ORIENTATION_PORTRAIT);  // 初始假设竖屏
    _pending = GESTURE_NONE;

    // 冷却 + 跨帧计时器一并清零 —— 校准 wizard 每次 ENTER 重录时调用
    _last_rotate_ms = 0;
    _last_face_ms   = 0;
    _last_shake_ms  = 0;
    _last_tap_ms    = 0;
    _peak_abs_accel = 0;
    _peak_window_start_ms = 0;
    _face_down_since_ms   = 0;
    _face_up_since_ms     = 0;
    _tap_prev_gz          = 1.0f;

    _shake_phase          = SHAKE_PHASE_IDLE;
    _shake_axis_sign      = 0;
    _shake_direction      = 0;
    _shake_settle_samples = 0;
    _shake_started_ms     = 0;
    _shake_baseline_ax    = 0.0f;
    _shake_baseline_valid = false;

    // 滑动窗也清空，避免上一步的残值污染本步
    _wx.reset();
    _wy.reset();
    _wz.reset();

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

GestureEvent GestureEngine::detectShake_(float ax, uint32_t now_ms) {
    const float start_threshold  = g_tuning.shake_threshold;
    const float return_threshold = g_tuning.shake_return_threshold;
    const float settle_threshold = g_tuning.shake_settle_threshold;

    if (_shake_phase != SHAKE_PHASE_IDLE &&
        now_ms - _shake_started_ms > g_tuning.shake_window_ms) {
        _shake_phase = SHAKE_PHASE_IDLE;
        _shake_axis_sign = 0;
        _shake_direction = 0;
        _shake_settle_samples = 0;
    }

    if (!_shake_baseline_valid) {
        _shake_baseline_ax = ax;
        _shake_baseline_valid = true;
        return GESTURE_NONE;
    }

    const float motion_ax = ax - _shake_baseline_ax;

    if (_shake_phase == SHAKE_PHASE_IDLE) {
        if (fabsf(motion_ax) < start_threshold) {
            // 静止时缓慢跟随安装角度/重力投影，不把约 0.20g 的偏置当作运动。
            _shake_baseline_ax += motion_ax * 0.08f;
            return GESTURE_NONE;
        }
        if (now_ms - _last_shake_ms < g_tuning.shake_cooldown_ms) return GESTURE_NONE;

        _shake_axis_sign = motion_ax > 0.0f ? 1 : -1;
        const bool positive_means_left = (g_tuning.shake_invert == 0);
        const bool is_left = (_shake_axis_sign > 0) == positive_means_left;
        _shake_direction = is_left ? 1 : -1;
        _shake_started_ms = now_ms;
        _shake_phase = SHAKE_PHASE_OUTBOUND;
        _peak_abs_accel = fabsf(motion_ax);
        return GESTURE_NONE;
    }

    if (_shake_phase == SHAKE_PHASE_OUTBOUND) {
        if (fabsf(motion_ax) > _peak_abs_accel) _peak_abs_accel = fabsf(motion_ax);
        if ((_shake_axis_sign > 0 && motion_ax <= -return_threshold) ||
            (_shake_axis_sign < 0 && motion_ax >= return_threshold)) {
            _shake_phase = SHAKE_PHASE_RETURNING;
            _shake_settle_samples = 0;
        }
        return GESTURE_NONE;
    }

    if (fabsf(motion_ax) <= settle_threshold) {
        if (++_shake_settle_samples < 3) return GESTURE_NONE;

        const GestureEvent event = _shake_direction > 0
            ? GESTURE_SHAKE_LEFT : GESTURE_SHAKE_RIGHT;
        _last_shake_ms = now_ms;
        _shake_phase = SHAKE_PHASE_IDLE;
        _shake_axis_sign = 0;
        _shake_direction = 0;
        _shake_settle_samples = 0;
        return event;
    }

    _shake_settle_samples = 0;
    return GESTURE_NONE;
}

bool GestureEngine::detectTap_(const AccelReading& acc, uint32_t now_ms) {
    // 简化：gz 在短时间内 spike > tap_z_high 算 tap（prev < tap_z_low）
    bool tap = (acc.z > g_tuning.tap_z_high && _tap_prev_gz < g_tuning.tap_z_low);
    _tap_prev_gz = acc.z;
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
        if (_face_down_since_ms == 0) {
            _face_down_since_ms = now_ms;
        }
        if ((now_ms - _face_down_since_ms) >= g_tuning.face_down_stable_ms) {
            if (now_ms - _last_face_ms >= g_tuning.face_cooldown_ms) {
                _last_face_ms = now_ms;
                _face_down_since_ms = 0;
                return GESTURE_FACE_DOWN;
            }
        }
    } else if (az < g_tuning.face_up_threshold) {
        // 翻回
        if (_face_up_since_ms == 0) {
            _face_up_since_ms = now_ms;
        }
        if ((now_ms - _face_up_since_ms) >= g_tuning.face_up_stable_ms) {
            if (now_ms - _last_face_ms >= g_tuning.face_cooldown_ms) {
                _last_face_ms = now_ms;
                _face_up_since_ms = 0;
                return GESTURE_FACE_UP_OPEN;
            }
        }
    } else {
        // 中性区：不重置 face_down/face_up 计时器 —— 它们各自的 if 分支
        // 会在 fire 时清零。这里留空。
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

    // 4) 摇动检测 —— 用原始 acc.x（不经滑动平均），否则平滑会压平摇动
    {
        GestureEvent sh = detectShake_(acc.x, now_ms);
        if (sh != GESTURE_NONE) return sh;
    }

    // 5) Tap
    if (detectTap_(acc, now_ms)) {
        return GESTURE_TAP;
    }

    return GESTURE_NONE;
}

} // namespace desknest
