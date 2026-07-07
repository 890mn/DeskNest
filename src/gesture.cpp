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
    .shake_threshold     = 0.8f,                              // 旧版单峰阈值 1.5g 偏严；
                                                               // 新版用零交叉 + 峰值，0.8g 已能区分"真摇动"和噪声
    .shake_window_ms     = 300,                               // 略放宽窗口到 300ms 容下 3-4 次摆
    .shake_cooldown_ms   = defaults::T_SHAKE_COOLDOWN_MS,    // 600
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

    // 摇动环形缓冲也清掉
    for (uint8_t i = 0; i < SHAKE_BUF; ++i) _shake_ax_buf[i] = 0;
    _shake_idx         = 0;
    _shake_filled      = false;
    _shake_win_start_ms = 0;

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
    // 摇动检测：零交叉计数 + 峰值 + 方向（ax 窗口 sum 的符号）
    //
    // 旧版只看 |a| 单峰是否 > shake_threshold（默认 1.5g）；真实摇动时 ax
    // 反复过零，峰值常到不了 1.5g，且 fire 瞬间 ax 符号随机，谈不上"左/右"。
    //
    // 新版（plan §4.2）：
    //   - 8 样本 ≈ 267ms 窗口内 ax 零交叉 ≥ 2（至少 1 个完整左右摆）
    //   - 窗口内 |ax| 峰值 > shake_threshold（默认 0.8g；wizard 可调）
    //   - 方向 = sum(ax) 符号：sum>0 → 主导正向 → SHAKE_LEFT
    //   - 冷却 600ms 防抖

    // 1) 窗口超时 → 标记为"待重新累积"（不动 _shake_idx，让它继续环形写）
    if (_shake_filled && (now_ms - _shake_win_start_ms) > g_tuning.shake_window_ms) {
        _shake_filled = false;
        _shake_idx = 0;       // 重新从 0 开始累积（之前的 8 样本作废）
    }

    // 2) 写一帧；首帧（_shake_idx==0 && !_shake_filled）记下窗口起点
    if (_shake_idx == 0 && !_shake_filled) {
        _shake_win_start_ms = now_ms;
    }
    _shake_ax_buf[_shake_idx] = ax;
    _shake_idx = (_shake_idx + 1) % SHAKE_BUF;
    if (_shake_idx == 0) _shake_filled = true;
    if (!_shake_filled) return GESTURE_NONE;   // 累积未满 8 帧，不判

    // 3) 零交叉 ≥ 2
    int zc = 0;
    float prev = _shake_ax_buf[0];
    for (uint8_t i = 1; i < SHAKE_BUF; ++i) {
        if ((prev > 0.0f) != (_shake_ax_buf[i] > 0.0f)) zc++;
        prev = _shake_ax_buf[i];
    }
    if (zc < 2) return GESTURE_NONE;

    // 4) 窗口内 |ax| 峰值
    float peak = 0.0f;
    for (uint8_t i = 0; i < SHAKE_BUF; ++i) {
        const float v = _shake_ax_buf[i];
        if (fabsf(v) > peak) peak = fabsf(v);
    }
    if (peak < g_tuning.shake_threshold) return GESTURE_NONE;

    // 5) 冷却
    if (now_ms - _last_shake_ms < g_tuning.shake_cooldown_ms) return GESTURE_NONE;

    // 6) 触发
    //   方向语义：用户先动 → 反方向拉回 = 一次完整摇动
    //   方向 = 第一帧 ax 的符号（用户初始移动方向）
    //   - ax[0] > 0  → GESTURE_SHAKE_LEFT  (约定：正 ax 对应"左")
    //   - ax[0] < 0  → GESTURE_SHAKE_RIGHT
    //   真机如果方向反了，wizard 会把 first_ax 显示出来，用户可以
    //   通过 g_tuning.shake_invert 或者改这里调整。
    _last_shake_ms  = now_ms;
    _shake_filled   = false;
    _shake_idx      = 0;       // 准备下一窗
    _peak_abs_accel = peak;
    return (_shake_ax_buf[0] > 0.0f) ? GESTURE_SHAKE_LEFT : GESTURE_SHAKE_RIGHT;
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
