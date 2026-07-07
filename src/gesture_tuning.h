// src/gesture_tuning.h
// 栖屏 DeskNest - 手势调参串口 REPL
// "栖于桌面，息于常亮之间"
//
// 行空板端调试用：通过 USB 串口交互式地：
//   - 改阈值（不重新烧录）
//   - 注入 accel 样本（脚本化回放抓到的真实序列）
//   - 把当前 accel + 事件流到串口（录日志分析）
//
// 工作流（真机）：
//   1. 烧录固件
//   2. `pio device monitor`（或任何串口终端，115200 baud）
//   3. 看到 'g>' 提示符 → `help` 列命令
//   4. `record` → 拿设备做翻面/旋转/摇动 → `stop`
//   5. 抓到日志后 `set <name> <v>` 试调，调好写回 src/config.h 重烧
//
// 设计原则：
//   - 非阻塞：dn_tuning_loop() 每次 main loop 调一次，吃掉 Serial 上能读到的字节
//   - 改阈值只动 g_tuning，gesture.cpp 下一帧就生效
//   - 跟状态机/UI 解耦：tuning 是旁路，不影响 SYSTEM_ACTIVE 主循环

#ifndef DESKNEST_GESTURE_TUNING_H
#define DESKNEST_GESTURE_TUNING_H

#include <stdint.h>

#include "config.h"
#include "sensors.h"
#include "gesture.h"

namespace desknest {

inline bool dn_tuning_event_matches(GestureEvent expected, GestureEvent actual) {
    if (actual == expected) return true;
    const bool expects_shake = expected == GESTURE_SHAKE_LEFT || expected == GESTURE_SHAKE_RIGHT;
    const bool got_shake = actual == GESTURE_SHAKE_LEFT || actual == GESTURE_SHAKE_RIGHT;
    return expects_shake && got_shake;
}

// 启动时打印 banner + 第一条 prompt
void dn_tuning_setup();

// 主循环每帧调用：消费串口输入；如有 pending feed，消费并返回它（替换真实 accel）
// 返回 true 表示本帧应该用 'out' 代替真实传感器读数
bool dn_tuning_take_feed(AccelReading& out);

// 主循环在 g_gesture.update() 之后调用：处理 recording 日志
void dn_tuning_post_step(uint32_t now_ms, const AccelReading& acc, GestureEvent e);

// 测试 / 脚本化入口：把一行命令塞进去，等价于用户在串口敲了再按回车
// host smoke test 用它直接验 'set' / 'feed' / 'reset' 的副作用
void dn_tuning_inject_command(const char* line);

} // namespace desknest

#endif // DESKNEST_GESTURE_TUNING_H
