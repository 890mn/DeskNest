// src/app.h
// 栖屏 DeskNest - 顶层应用接口
// "栖于桌面，息于常亮之间"
//
// 由 DeskNest.ino（Arduino 入口）调用。

#ifndef DESKNEST_APP_H
#define DESKNEST_APP_H

#ifdef __cplusplus
extern "C" {
#endif

// Arduino 入口：setup 时调用一次
void dn_app_setup();

// Arduino 主循环：每帧调用
void dn_app_loop();

#ifdef __cplusplus
}
#endif

#endif // DESKNEST_APP_H
