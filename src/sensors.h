// src/sensors.h
// 栖屏 DeskNest - 传感器读取（温湿度 / 光照 / 三轴加速度 / 电池）
// "栖于桌面，息于常亮之间"
//
// P0-B：所有传感器走 1Hz polling；加速度走 30Hz（手势识别用）。
// 真实硬件依赖 `unihiker_k10.h` 提供的 k10.aht20 / k10.ltr303 / k10.accelero
// 等高层 API；本文件用统一接口抽象，便于测试与替换。
//
// 实际 K10 BSP 的具体方法名在装好 BSP 后需要核对；若高层 API 不可用，
// 则回退到 DFRobot_AHT20 / DFRobot_LTR303 / DFRobot_SC7A20 等独立库。

#ifndef DESKNEST_SENSORS_H
#define DESKNEST_SENSORS_H

#include "config.h"

#include <Arduino.h>

namespace desknest {

// ============================================================================
// 数据结构
// ============================================================================

struct AHT20Reading {
    bool    valid;
    float   temperatureC;   // 摄氏度
    float   humidityPct;    // 0-100
    uint32_t fetchedAtMs;
};

struct LTR303Reading {
    bool    valid;
    float   lux;            // 流明
    uint16_t rawAls;        // 原始 ALS 值
    uint32_t fetchedAtMs;
};

struct AccelReading {
    bool    valid;
    float   x, y, z;        // 单位 g（含重力）
    uint32_t fetchedAtMs;
};

struct BatteryReading {
    bool    valid;
    uint8_t percent;        // 0-100
    float   voltage;        // 伏
    bool    charging;       // 简化：vbat 上升算充电中
    uint32_t fetchedAtMs;
};

// ============================================================================
// Sensors 单例接口
// ============================================================================

class Sensors {
public:
    // 初始化（setup 时调用）
    void begin();

    // 每帧调用（在 Core 1 跑）
    // 高频：加速度 @ 30Hz；低频：AHT20/LTR303/Battery @ 1Hz
    void update();

    // 取最新读数（线程安全：本类仅在 Core 1 跑，无需锁）
    AHT20Reading aht20()   const { return _aht; }
    LTR303Reading ltr303()  const { return _lux; }
    AccelReading accel()   const { return _acc; }
    BatteryReading battery() const { return _bat; }

    // 强制立即读一次（开机 / 唤醒时用）
    void requestImmediateAht20()  { _aht_due_ms = 0; }
    void requestImmediateLtr303() { _lux_due_ms = 0; }
    void requestImmediateBattery(){ _bat_due_ms = 0; }

    // 自测：在 begin() 后调一次，打印 noise / range / 决策统计
    void selfTest(uint32_t duration_ms = 10000);

private:
    void readAht20_();
    void readLtr303_();
    void readAccel_();
    void readBattery_();

    // 缓存
    AHT20Reading  _aht  = { false, 0, 0, 0 };
    LTR303Reading _lux  = { false, 0, 0, 0 };
    AccelReading  _acc  = { false, 0, 0, 0, 0 };
    BatteryReading _bat = { false, 0, 0, false, 0 };

    // 1Hz 节流
    uint32_t _aht_due_ms  = 0;
    uint32_t _lux_due_ms  = 0;
    uint32_t _bat_due_ms  = 0;

    // 30Hz 加速度
    uint32_t _acc_period_ms = 1000 / 30;  // ≈ 33ms

    // 启动标志
    bool _initialized = false;
};

// 全局单例
extern Sensors g_sensors;

} // namespace desknest

#endif // DESKNEST_SENSORS_H
