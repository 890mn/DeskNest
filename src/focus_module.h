// src/focus_module.h
// 栖屏 DeskNest - focus status module
//
// 当前阶段提供默认专注页状态；后续番茄钟/暂停/完成逻辑在此扩展，
// UI model 只消费 FocusStatus。

#ifndef DESKNEST_FOCUS_MODULE_H
#define DESKNEST_FOCUS_MODULE_H

#include <stdint.h>

namespace desknest {

enum FocusState : uint8_t {
    FOCUS_IDLE = 0,
    FOCUS_RUNNING,
    FOCUS_PAUSED,
    FOCUS_DONE,
};

struct FocusStatus {
    const char* modeText = "";
    const char* timerText = "";
    FocusState state = FOCUS_IDLE;
    const char* stateText = "";
    const char* goalText = "";
};

inline const char* dn_focus_state_text(FocusState state) {
    switch (state) {
        case FOCUS_IDLE:    return "IDLE";
        case FOCUS_RUNNING: return "> IN PROGRESS";
        case FOCUS_PAUSED:  return "PAUSED";
        case FOCUS_DONE:    return "DONE";
    }
    return "?";
}

inline FocusStatus dn_focus_default_status() {
    FocusStatus status;
    status.modeText = "DEEP WORK";
    status.timerText = "25:00";
    status.state = FOCUS_RUNNING;
    status.stateText = dn_focus_state_text(status.state);
    status.goalText = "Goal · 50 min";
    return status;
}

} // namespace desknest

#endif // DESKNEST_FOCUS_MODULE_H

