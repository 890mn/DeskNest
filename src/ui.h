// src/ui.h
// 栖屏 DeskNest - UI 渲染层
// "栖于桌面，息于常亮之间"
//
// 8 个 UIPage + RGB LED 状态指示。P0-D 阶段先把所有页画出来，
// P1 再优化按需刷新和动画。

#ifndef DESKNEST_UI_H
#define DESKNEST_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 在 dn_app_setup 里调一次：初始化屏幕 + 创建 canvas
void dn_ui_setup();

// 在 dn_app_loop 里调：按当前页面 dispatch 渲染（内部按页面变化/1Hz 节流）
void dn_ui_render();

// Binary K10 LCD backlight control. Content remains rendered while off so a
// wake can restore the current page without rebuilding it.
void dn_ui_set_backlight(bool enabled);

#ifdef __cplusplus
}
#endif

#endif // DESKNEST_UI_H
