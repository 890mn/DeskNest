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
    _s.orientation   = ORIENTATION_PORTRAIT;
    _s.page          = PAGE_PORTRAIT_OVERVIEW;
    _s.rotLock       = ROT_AUTO;
    _s.lastInputMs   = millis();
    _temp_unlock_expire_ms = 0;

    Serial.println("[D][STATE] begin → SYSTEM_ACTIVE / PORTRAIT / OVERVIEW");
}

// ============================================================================
// update
// ============================================================================

void StateMachine::update(GestureEvent g, ButtonEvent b, OrientationState detected,
                          uint32_t now_ms) {
    // 0) 临时解锁过期检查
    if (_s.rotLock == ROT_LOCKED_TEMP_5S && now_ms >= _temp_unlock_expire_ms) {
        // 5s 到了：恢复到之前的锁定（简化：直接回 ROT_AUTO）
        _s.rotLock = ROT_AUTO;
        Serial.println("[D][STATE] rot lock temp 5s expired → AUTO");
    }

    // 1) 翻面 / 打开恢复：最优先
    if (g == GESTURE_FACE_DOWN) {
        _s.orientation = ORIENTATION_FACE_DOWN;
        _s.system      = SYSTEM_FACE_DOWN_SLEEP;
        _s.page        = PAGE_SLEEP_FACE_DOWN;
        Serial.println("[D][STATE] → FACE_DOWN_SLEEP");
        return;
    }
    if (g == GESTURE_FACE_UP_OPEN) {
        _s.system = SYSTEM_ACTIVE;
        _s.orientation = detected;  // 用实际姿态
        _s.page = (detected == ORIENTATION_LANDSCAPE)
                  ? PAGE_LANDSCAPE_FOCUS
                  : PAGE_PORTRAIT_OVERVIEW;
        _s.lastInputMs = now_ms;
        Serial.printf("[D][STATE] → ACTIVE / %s / %s (from face up)\n",
                      detected == ORIENTATION_LANDSCAPE ? "LANDSCAPE" : "PORTRAIT",
                      _s.page == PAGE_LANDSCAPE_FOCUS ? "FOCUS" : "OVERVIEW");
        return;
    }

    // 翻面状态下：忽略 SHAKE/按键（除 BUTTON_FACTORY）。
    // ⚠ 不要删这个 return：BUTTON_FACTORY 在这里单独处理，然后早 return，
    // 下面 applyButton_ 不应再被调到（防止 FACOTRY 被 applyButton_ 二次 switch 走错分支）。
    if (_s.system == SYSTEM_FACE_DOWN_SLEEP) {
        if (b == BUTTON_FACTORY) {
            Serial.println("[D][STATE] factory reset triggered from face-down");
            // 工厂复位由 SETTINGS 处理；这里只恢复 ACTIVE
            _s.system = SYSTEM_ACTIVE;
            _s.orientation = detected;
            _s.page = PAGE_PORTRAIT_SETTINGS;
            _s.lastInputMs = now_ms;
        }
        return;
    }

    // 2) 按键处理（BUTTON_FIRST 模式 / 默认）
    if (b != BUTTON_NONE) {
        applyButton_(b, now_ms);
    }

    // 3) 手势处理（按键有事件时不处理手势，避免抢占）
    if (b == BUTTON_NONE && g != GESTURE_NONE) {
        applyGesture_(g, now_ms);
    }

    // 4) 姿态同步（独立事件流）
    applyOrientation_(detected, now_ms);

    // 5) 电源档位超时
    applyPowerTimeout_(now_ms);
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

    // 切换：跳到对应姿态的默认页
    const OrientationState old = _s.orientation;
    _s.orientation = detected;
    if (detected == ORIENTATION_LANDSCAPE) {
        if (old == ORIENTATION_PORTRAIT) {
            _s.page = PAGE_LANDSCAPE_FOCUS;
        }
    } else {
        if (old == ORIENTATION_LANDSCAPE) {
            _s.page = PAGE_PORTRAIT_OVERVIEW;
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
