// src/buttons.h
// 栖屏 DeskNest - 按键轮询（K10 BSP）
// "栖于桌面，息于常亮之间"
//
// 用 K10 BSP 的 k10.buttonA / buttonB / buttonAB ->isPressed()，
// 不直接操作 GPIO。引脚由 BSP 内部管（eP5_KeyA=12, eP11_KeyB=2）。
//
// 边沿检测 + 长按识别 —— 一帧返回一个 ButtonEvent。
// 短按阈值 1000ms，长按 1000ms（released 前若已 long 则不补发短按）；
// A+B 短按 BUTTON_SELECT，A+B 长按 3s BUTTON_FACTORY。

#ifndef DESKNEST_BUTTONS_H
#define DESKNEST_BUTTONS_H

#include <stdint.h>

// config.h 里的 ButtonEvent 在 namespace desknest 里；不包 extern "C"
#include "config.h"

namespace desknest {

// 主循环每帧调用一次；返回本帧待处理的事件（NONE = 无）
ButtonEvent dn_button_poll(uint32_t now_ms);

}  // namespace desknest

#endif // DESKNEST_BUTTONS_H