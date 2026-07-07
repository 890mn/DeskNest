// src/state_machine.cpp
// 栖屏 DeskNest - 4 轴状态机实现
// "栖于桌面，息于常亮之间"

#include "state_machine.h"

#include <Arduino.h>

namespace desknest {

StateMachine g_state;

// ============================================================================
// 页面循环
// ============================================================================

UIPage StateMachine::nextPortrait(UIPage p) {
    // 循环：OVERVIEW → AI_USAGE → ENVIRONMENT → SETTINGS → OVERVIEW
    switch (p) {
        case PAGE_PORTRAIT_OVERVIEW:    return PAGE_PORTRAIT_AI_USAGE;
        case PAGE_PORTRAIT_AI_USAGE:    return PAGE_PORTRAIT_ENVIRONMENT;
        case PAGE_PORTRAIT_ENVIRONMENT: return PAGE_PORTRAIT_SETTINGS;
        case PAGE_PORTRAIT_SETTINGS:    return PAGE_PORTRAIT_OVERVIEW;
        default:                        return PAGE_PORTRAIT_OVERVIEW;
    }
}

UIPage StateMachine::prevPortrait(UIPage p) {
    switch (p) {
        case PAGE_PORTRAIT_OVERVIEW:    return PAGE_PORTRAIT_SETTINGS;
        case PAGE_PORTRAIT_AI_USAGE:    return PAGE_PORTRAIT_OVERVIEW;
        case PAGE_PORTRAIT_ENVIRONMENT: return PAGE_PORTRAIT_AI_USAGE;
        case PAGE_PORTRAIT_SETTINGS:    return PAGE_PORTRAIT_ENVIRONMENT;
        default:                        return PAGE_PORTRAIT_OVERVIEW;
    }
}

UIPage StateMachine::nextLandscape(UIPage p) {
    switch (p) {
        case PAGE_LANDSCAPE_OVERVIEW: return PAGE_LANDSCAPE_FOCUS;
        case PAGE_LANDSCAPE_FOCUS:    return PAGE_LANDSCAPE_CUSTOM;
        case PAGE_LANDSCAPE_CUSTOM:   return PAGE_LANDSCAPE_OVERVIEW;
        default:                      return PAGE_LANDSCAPE_OVERVIEW;
    }
}

UIPage StateMachine::prevLandscape(UIPage p) {
    switch (p) {
        case PAGE_LANDSCAPE_OVERVIEW: return PAGE_LANDSCAPE_CUSTOM;
        case PAGE_LANDSCAPE_FOCUS:    return PAGE_LANDSCAPE_OVERVIEW;
        case PAGE_LANDSCAPE_CUSTOM:   return PAGE_LANDSCAPE_FOCUS;
        default:                      return PAGE_LANDSCAPE_OVERVIEW;
    }
}

// ============================================================================
// begin
// ============================================================================

void StateMachine::begin() {
    _s.system        = SYSTEM_ACTIVE;
    _s.face_state    = FACE_STATE_UP;        // 显式：开机默认 face_up
    _s.orientation   = ORIENTATION_PORTRAIT;
    _s.page          = PAGE_PORTRAIT_OVERVIEW;
    _s.rotLock       = ROT_AUTO;
    _s.lastInputMs   = millis();
    _temp_unlock_expire_ms = 0;

    Serial.println("[D][STATE] begin → FACE_UP / ACTIVE / PORTRAIT / OVERVIEW");
}

// ============================================================================
// 各路输入入口（每个独立，从 IDLE 出发）
// ============================================================================
//
// 设计：每个 update*() 都从当前快照 _s 出发独立处理，不互相抑制。
// 同一帧多路输入都 fire 的情况：由调用方在 update() 里控制顺序，
// 后调的覆盖前面的（同字段写）。用户可在 platformio.ini 关掉任意一路。

void StateMachine::applyFace_(GestureEvent face, uint32_t now_ms) {
    if (face == GESTURE_FACE_DOWN) {
        // 已 face-down 还来 face-down = 加速度计噪声，无视
        if (_s.face_state == FACE_STATE_DOWN) return;
        _s.pre_face_down_page = _s.page;
        _s.face_state = FACE_STATE_DOWN;
        _s.orientation = ORIENTATION_FACE_DOWN;
        _s.system      = SYSTEM_FACE_DOWN_SLEEP;
        _s.page        = PAGE_SLEEP_FACE_DOWN;
        Serial.printf("[D][STATE] → FACE_DOWN_SLEEP (saved page %d)\n",
                      (int)_s.pre_face_down_page);
        return;
    }
    if (face == GESTURE_FACE_UP_OPEN) {
        // 已 face-up 还来 face-up = 加速度计噪声，无视（否则会把当前页
        // 误覆盖回 pre_face_down_page，破坏导航）
        if (_s.face_state == FACE_STATE_UP) return;
        _s.face_state = FACE_STATE_UP;
        _s.system = SYSTEM_ACTIVE;
        _s.page   = _s.pre_face_down_page;
        _s.lastInputMs = now_ms;
        Serial.printf("[D][STATE] → ACTIVE (face up, page %d restored)\n",
                      (int)_s.page);
        return;
    }
}

#if ENABLE_FACE_INPUT
void StateMachine::updateFace(GestureEvent face, uint32_t now_ms) {
    applyFace_(face, now_ms);
}
#endif

#if ENABLE_GESTURE_INPUT
void StateMachine::updateGesture(GestureEvent g, uint32_t now_ms) {
    // 层级 gate：face_down 时所有手势输入都屏蔽
    if (_s.face_state == FACE_STATE_DOWN) return;
    if (g == GESTURE_NONE) return;
    applyGesture_(g, now_ms);
}
#endif

#if ENABLE_BUTTON_INPUT
void StateMachine::updateButton(ButtonEvent b, uint32_t now_ms) {
    // 层级 gate：face_down 时吞掉所有按键（除 FACTORY）
    if (_s.face_state == FACE_STATE_DOWN) {
        if (b == BUTTON_FACTORY) {
            Serial.println("[D][STATE] factory reset triggered from face-down");
            _s.face_state = FACE_STATE_UP;
            _s.system = SYSTEM_ACTIVE;
            _s.orientation = g_gesture.orientation();
            _s.page = PAGE_PORTRAIT_SETTINGS;
            _s.lastInputMs = now_ms;
        }
        return;
    }
    if (b == BUTTON_NONE) return;
    applyButton_(b, now_ms);
}
#endif

#if ENABLE_ORIENTATION_INPUT
void StateMachine::updateOrientation(OrientationState detected, uint32_t now_ms) {
    // 层级 gate：face_down 时跳过朝向检测（face 优先）
    if (_s.face_state == FACE_STATE_DOWN) return;
    applyOrientation_(detected, now_ms);
}
#endif

#if ENABLE_POWER_TIMEOUT
void StateMachine::tickPowerTimeout(uint32_t now_ms) {
    // 临时解锁过期
    if (_s.rotLock == ROT_LOCKED_TEMP_5S && now_ms >= _temp_unlock_expire_ms) {
        _s.rotLock = ROT_AUTO;
        Serial.println("[D][STATE] rot lock temp 5s expired → AUTO");
    }
    applyPowerTimeout_(now_ms);
}
#endif

// ============================================================================
// update —— 旧入口，按优先级依次调各 update*()
// ============================================================================
//
// 顺序：face → orientation → button → gesture → power
// 同帧多路：face 最先；其它按调用顺序覆盖（同字段写，后到赢）
// 注意：button 和 gesture 都不再互相抑制 —— 各自独立处理

void StateMachine::update(GestureEvent g, ButtonEvent b, OrientationState detected,
                          uint32_t now_ms) {
#if ENABLE_FACE_INPUT
    updateFace(g, now_ms);
#endif

#if ENABLE_ORIENTATION_INPUT
    updateOrientation(detected, now_ms);
#endif

#if ENABLE_BUTTON_INPUT
    updateButton(b, now_ms);
#endif

#if ENABLE_GESTURE_INPUT
    // 手势不再被按键抑制 —— 各自独立 fire；同帧后调覆盖前调
    updateGesture(g, now_ms);
#endif

#if ENABLE_POWER_TIMEOUT
    tickPowerTimeout(now_ms);
#endif
}

// ============================================================================
// 内部：按键
// ============================================================================

void StateMachine::applyButton_(ButtonEvent b, uint32_t now_ms) {
    _s.lastInputMs = now_ms;
    // FACE_DOWN 已被外层 return 处理
    // 任何按键 → 立即回 ACTIVE（如果是 AMBIENT/LIGHT_SLEEP）
    if (_s.system == SYSTEM_AMBIENT || _s.system == SYSTEM_LIGHT_SLEEP) {
        _s.system = SYSTEM_ACTIVE;
    }

    switch (b) {
        case BUTTON_NEXT: {
            if (_s.orientation == ORIENTATION_LANDSCAPE) {
                _s.page = nextLandscape(_s.page);
            } else {
                _s.page = nextPortrait(_s.page);
            }
            Serial.printf("[D][STATE] button NEXT → page=%d\n", (int)_s.page);
            break;
        }
        case BUTTON_PREV: {
            if (_s.orientation == ORIENTATION_LANDSCAPE) {
                _s.page = prevLandscape(_s.page);
            } else {
                _s.page = prevPortrait(_s.page);
            }
            Serial.printf("[D][STATE] button PREV → page=%d\n", (int)_s.page);
            break;
        }
        case BUTTON_SELECT: {
            // A+B 同时：临时解锁 5s
            if (_s.rotLock == ROT_LOCKED_PORTRAIT || _s.rotLock == ROT_LOCKED_LANDSCAPE) {
                _s.rotLock = ROT_LOCKED_TEMP_5S;
                _temp_unlock_expire_ms = now_ms + 5000;
                Serial.println("[D][STATE] A+B → temp unlock 5s");
            }
            break;
        }
        case BUTTON_BACK: {
            // B 长按 1s：返回主总览
            if (_s.orientation == ORIENTATION_LANDSCAPE) {
                _s.page = PAGE_LANDSCAPE_OVERVIEW;
            } else {
                _s.page = PAGE_PORTRAIT_OVERVIEW;
            }
            Serial.println("[D][STATE] button BACK → OVERVIEW");
            break;
        }
        case BUTTON_MENU: {
            // A 长按 1s：进入 SETTINGS
            _s.page = PAGE_PORTRAIT_SETTINGS;
            Serial.println("[D][STATE] button MENU → SETTINGS");
            break;
        }
        case BUTTON_FACTORY: {
            // A+B 长按 3s：工厂复位入口（实际重置由 storage 处理）
            Serial.println("[D][STATE] button FACTORY → 工厂复位入口");
            _s.page = PAGE_PORTRAIT_SETTINGS;
            break;
        }
        default: break;
    }
}

// ============================================================================
// 内部：手势
// ============================================================================

void StateMachine::applyGesture_(GestureEvent g, uint32_t now_ms) {
    _s.lastInputMs = now_ms;
    if (_s.system == SYSTEM_AMBIENT || _s.system == SYSTEM_LIGHT_SLEEP) {
        _s.system = SYSTEM_ACTIVE;
    }

    switch (g) {
        case GESTURE_SHAKE_LEFT: {
            if (_s.orientation == ORIENTATION_LANDSCAPE) {
                _s.page = prevLandscape(_s.page);
            } else {
                _s.page = prevPortrait(_s.page);
            }
            Serial.printf("[D][STATE] gesture SHAKE_LEFT → page=%d\n", (int)_s.page);
            break;
        }
        case GESTURE_SHAKE_RIGHT: {
            if (_s.orientation == ORIENTATION_LANDSCAPE) {
                _s.page = nextLandscape(_s.page);
            } else {
                _s.page = nextPortrait(_s.page);
            }
            Serial.printf("[D][STATE] gesture SHAKE_RIGHT → page=%d\n", (int)_s.page);
            break;
        }
        case GESTURE_TAP: {
            // 手动同步（简化：仅日志）
            Serial.println("[D][STATE] gesture TAP → request sync (mock)");
            break;
        }
        case GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE: {
            if (_s.rotLock == ROT_LOCKED_LANDSCAPE ||
                _s.rotLock == ROT_LOCKED_TEMP_5S) {
                // 允许进入 LANDSCAPE
                _s.orientation = ORIENTATION_LANDSCAPE;
                _s.page = PAGE_LANDSCAPE_FOCUS;
                Serial.println("[D][STATE] rotate P→L → LANDSCAPE_FOCUS");
            } else if (_s.rotLock == ROT_LOCKED_PORTRAIT) {
                Serial.println("[D][STATE] rotate blocked by ROT_LOCKED_PORTRAIT");
            } else {
                _s.orientation = ORIENTATION_LANDSCAPE;
                _s.page = PAGE_LANDSCAPE_FOCUS;
                Serial.println("[D][STATE] rotate P→L → LANDSCAPE_FOCUS");
            }
            break;
        }
        case GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT: {
            if (_s.rotLock == ROT_LOCKED_PORTRAIT) {
                // 已锁竖屏，但当前是横屏：跳过去
                _s.orientation = ORIENTATION_PORTRAIT;
                _s.page = PAGE_PORTRAIT_OVERVIEW;
            } else {
                _s.orientation = ORIENTATION_PORTRAIT;
                _s.page = PAGE_PORTRAIT_OVERVIEW;
                Serial.println("[D][STATE] rotate L→P → PORTRAIT_OVERVIEW");
            }
            break;
        }
        default: break;
    }
}

// ============================================================================
// 内部：姿态同步
// ============================================================================

void StateMachine::applyOrientation_(OrientationState detected, uint32_t now_ms) {
    if (detected == ORIENTATION_UNKNOWN) return;
    if (detected == ORIENTATION_FACE_DOWN) return;  // 翻面已在外层处理
    if (_s.orientation == detected) return;
    if (_s.system == SYSTEM_FACE_DOWN_SLEEP) return;

    // 锁定检查
    if (_s.rotLock == ROT_LOCKED_PORTRAIT && detected == ORIENTATION_LANDSCAPE) return;
    if (_s.rotLock == ROT_LOCKED_LANDSCAPE && detected == ORIENTATION_PORTRAIT) return;

    // 切换：记忆模式 —— 每种姿态记下自己当前页面，转回来时恢复
    const OrientationState old = _s.orientation;
    _s.orientation = detected;
    if (detected == ORIENTATION_LANDSCAPE) {
        if (old == ORIENTATION_PORTRAIT) {
            // P→L：保存当前页到 portrait 记忆，去横屏记忆的页
            _s.last_portrait_page = _s.page;
            _s.page = _s.last_landscape_page;
        }
    } else {
        if (old == ORIENTATION_LANDSCAPE) {
            // L→P：保存当前页到 landscape 记忆，回到 portrait 记忆的页
            _s.last_landscape_page = _s.page;
            _s.page = _s.last_portrait_page;
        }
    }
    Serial.printf("[D][STATE] orient %d→%d page=%d\n",
                  (int)old, (int)detected, (int)_s.page);
    (void)now_ms;
}

// ============================================================================
// 内部：电源档位超时
// ============================================================================

void StateMachine::applyPowerTimeout_(uint32_t now_ms) {
    if (_s.system == SYSTEM_FACE_DOWN_SLEEP) return;
    if (_s.system == SYSTEM_CONFIG) return;

    const uint32_t idle_ms = now_ms - _s.lastInputMs;

    // PowerProfile 暂未接 NVS，先固定 BALANCED；P1 阶段改成查表
    constexpr uint32_t T_AMBIENT = defaults::T_AMBIENT_MS_BALANCED;
    constexpr uint32_t T_SLEEP   = defaults::T_SLEEP_MS_BALANCED;

    if (_s.system == SYSTEM_ACTIVE && idle_ms >= T_AMBIENT) {
        _s.system = SYSTEM_AMBIENT;
        Serial.printf("[D][STATE] idle %lus → AMBIENT\n",
                      (unsigned long)(T_AMBIENT / 1000));
    } else if (_s.system == SYSTEM_AMBIENT && idle_ms >= T_SLEEP) {
        _s.system = SYSTEM_LIGHT_SLEEP;
        Serial.printf("[D][STATE] idle %lus → LIGHT_SLEEP\n",
                      (unsigned long)(T_SLEEP / 1000));
    }
}

} // namespace desknest
