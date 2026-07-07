// src/sensors.cpp
// 栖屏 DeskNest - 传感器实现（K10 BSP 真硬件路径）
// "栖于桌面，息于常亮之间"
//
// 实现策略：
//   1) 直接用 unihiker_k10 BSP 提供的 k10.* 高层 API
//   2) 频率：加速度随 Sensors::update() 节流到 30Hz；
//           AHT20 / LTR303 / Battery 1Hz
//   3) 加速度原始值是 12-bit LSB（SC7A20H 默认 ±2g → 1024 LSB/g），
//      这里除以 1024 转成 g 再喂给 GestureEngine。
//   4) K10 出厂无内置电池，readBattery_ 返回 invalid 占位。
//
// 真机烧录目标：DFRobot UNIHIKER K10（ESP32-S3 N16R8）。

#include "sensors.h"
#include "config.h"
#include "gesture.h"

#include <Arduino.h>
#include <unihiker_k10.h>

namespace desknest {

Sensors g_sensors;

// ---------------------------------------------------------------------------
// BSP 全局实例
//   - AHT20 的构造函数会自动起一个 1Hz 测量任务（unihiker_k10.cpp:132），
//     getData() 在首次测量完成前返回 -1。
//   - UNIHIKER_K10::begin() 创建 mutex / I2C / 按钮 / gesture_task（10Hz）
//     等基础，必须先调才能用任何传感器。
// ---------------------------------------------------------------------------
UNIHIKER_K10 k10;
AHT20        aht;

// SC7A20H 默认量程 ±2g：12-bit 输出对应 1024 LSB/g
constexpr float SC7A20H_LSB_PER_G = 1024.0f;

// ============================================================================
// begin
// ============================================================================

void Sensors::begin() {
    Serial.println("[D][SENSORS] init K10 BSP...");
    k10.begin();
    _initialized = true;

    // 首帧加速度：gesture engine 启动后立刻能拿到第一组数据
    readAccel_();

    Serial.println("[D][SENSORS] K10 BSP ready");
    Serial.println("[D][SENSORS]   AHT20 @0x38 (1Hz auto-task)");
    Serial.println("[D][SENSORS]   LTR303ALS @0x29 (auto-init)");
    Serial.println("[D][SENSORS]   SC7A20H @0x19 (auto-init, ~10Hz gesture_task)");
    Serial.println("[D][SENSORS]   battery: K10 无内置电池，返回 invalid");
}

// ============================================================================
// AHT20 温湿度
// ============================================================================

void Sensors::readAht20_() {
    // getData() 在 AHT20 任务首次完成测量前返回 -1；视为无效
    float t = aht.getData(AHT20::eAHT20TempC);
    float h = aht.getData(AHT20::eAHT20HumiRH);
    _aht.valid        = (t > -40.0f) && (h >= 0.0f);
    _aht.temperatureC = t;
    _aht.humidityPct  = h;
    _aht.fetchedAtMs  = millis();
}

// ============================================================================
// LTR303ALS 光照（BSP 已做 lux 估算，直接用）
// ============================================================================

void Sensors::readLtr303_() {
    uint16_t als = k10.readALS();
    _lux.valid       = true;
    _lux.rawAls      = als;
    _lux.lux         = (float)als;
    _lux.fetchedAtMs = millis();
}

// ============================================================================
// SC7A20H 三轴加速度（LSB → g）
// ============================================================================

void Sensors::readAccel_() {
    int x_raw = k10.getAccelerometerX();
    int y_raw = k10.getAccelerometerY();
    int z_raw = k10.getAccelerometerZ();

    _acc.valid       = true;
    _acc.x           = (float)x_raw / SC7A20H_LSB_PER_G;
    _acc.y           = (float)y_raw / SC7A20H_LSB_PER_G;
    _acc.z           = (float)z_raw / SC7A20H_LSB_PER_G;
    _acc.fetchedAtMs = millis();
}

// ============================================================================
// 电池（K10 无内置电池，占位）
// ============================================================================

void Sensors::readBattery_() {
    // TODO: 原理图复核 BATTERY_ADC（pins::BATTERY_ADC）实际引脚后启用。
    // 当前返回 invalid，避免 UI 显示噪声。
    _bat.valid       = false;
    _bat.percent     = 0;
    _bat.voltage     = 0.0f;
    _bat.charging    = false;
    _bat.fetchedAtMs = millis();
}

// ============================================================================
// update —— 调度入口
// ============================================================================

void Sensors::update() {
    if (!_initialized) return;

    const uint32_t now = millis();

    // 加速度 30Hz（节流）
    static uint32_t last_acc_ms = 0;
    if (now - last_acc_ms >= _acc_period_ms) {
        last_acc_ms = now;
        readAccel_();
    }

    // AHT20 / LTR303 / Battery 1Hz
    if (now >= _aht_due_ms) {
        _aht_due_ms = now + 1000;
        readAht20_();
    }
    if (now >= _lux_due_ms) {
        _lux_due_ms = now + 1000;
        readLtr303_();
    }
    if (now >= _bat_due_ms) {
        _bat_due_ms = now + 1000;
        readBattery_();
    }
}

// ============================================================================
// selfTest —— 3 轴加速度传感器封装自测
// ============================================================================
//
// 用途：定位"不稳定"问题的源头。分三个阶段：
//   Phase 1（10s）：静止放置，每 100ms 采一次（对齐 BSP gesture_task 节拍），
//                  打印每个轴的 avg / std / range / |a|。从 std 可看出噪声。
//   Phase 2（1s）：  BSP isGesture() 总触发次数；正常静止应全部为 0。
//   Phase 3（5s）：  DeskNest GestureEngine 决策：face-down / face-up / rotate / shake
//                  各触发多少次；从异常触发可定位 Engine bug。
//
// 用法：Sensors::begin() 之后立刻调 Sensors::selfTest()。

void Sensors::selfTest(uint32_t duration_ms) {
    using namespace desknest;
    Serial.println();
    Serial.println("=========================================");
    Serial.println("  3 轴传感器封装自测  Sensors::selfTest");
    Serial.println("=========================================");
    Serial.printf("[SELFTEST] Phase 1 将在 %lu ms 内、每 100ms 采样一次\n", duration_ms);
    Serial.println("[SELFTEST] 请保持设备静止平放在桌面上");
    Serial.println();

    // ------------------------------------------------------------------
    // Phase 1: 静止噪声 + 量程
    // ------------------------------------------------------------------
    constexpr int N = 100;  // 100 * 100ms = 10s
    float xs[N], ys[N], zs[N], mags[N];

    for (int i = 0; i < N; i++) {
        int x_raw = k10.getAccelerometerX();
        int y_raw = k10.getAccelerometerY();
        int z_raw = k10.getAccelerometerZ();

        xs[i] = (float)x_raw / SC7A20H_LSB_PER_G;
        ys[i] = (float)y_raw / SC7A20H_LSB_PER_G;
        zs[i] = (float)z_raw / SC7A20H_LSB_PER_G;
        mags[i] = sqrtf(xs[i]*xs[i] + ys[i]*ys[i] + zs[i]*zs[i]);

        // 每 10 个采样打印一次（避免日志爆炸）
        if (i % 10 == 0) {
            Serial.printf("[SELFTEST] P1 %3d/%d: raw(%5d,%5d,%5d) g(%+.3f,%+.3f,%+.3f) |a|=%.3f\n",
                          i, N, x_raw, y_raw, z_raw,
                          xs[i], ys[i], zs[i], mags[i]);
        }
        delay(100);
    }

    // 统计
    float x_min=xs[0], x_max=xs[0], x_sum=0;
    float y_min=ys[0], y_max=ys[0], y_sum=0;
    float z_min=zs[0], z_max=zs[0], z_sum=0;
    float m_min=mags[0], m_max=mags[0], m_sum=0;
    for (int i = 0; i < N; i++) {
        if (xs[i]<x_min) x_min=xs[i]; if (xs[i]>x_max) x_max=xs[i]; x_sum += xs[i];
        if (ys[i]<y_min) y_min=ys[i]; if (ys[i]>y_max) y_max=ys[i]; y_sum += ys[i];
        if (zs[i]<z_min) z_min=zs[i]; if (zs[i]>z_max) z_max=zs[i]; z_sum += zs[i];
        if (mags[i]<m_min) m_min=mags[i]; if (mags[i]>m_max) m_max=mags[i]; m_sum += mags[i];
    }
    float x_avg=x_sum/N, y_avg=y_sum/N, z_avg=z_sum/N, m_avg=m_sum/N;
    float x_var=0, y_var=0, z_var=0;
    for (int i = 0; i < N; i++) {
        x_var += (xs[i]-x_avg)*(xs[i]-x_avg);
        y_var += (ys[i]-y_avg)*(ys[i]-y_avg);
        z_var += (zs[i]-z_avg)*(zs[i]-z_avg);
    }
    float x_std=sqrtf(x_var/N), y_std=sqrtf(y_var/N), z_std=sqrtf(z_var/N);

    Serial.println();
    Serial.println("[SELFTEST] ---- Phase 1 统计 ----");
    Serial.printf("[SELFTEST]   X: avg=%+.3f std=%.4f range=[%+.3f, %+.3f] pp=%.4f g\n",
                  x_avg, x_std, x_min, x_max, x_max-x_min);
    Serial.printf("[SELFTEST]   Y: avg=%+.3f std=%.4f range=[%+.3f, %+.3f] pp=%.4f g\n",
                  y_avg, y_std, y_min, y_max, y_max-y_min);
    Serial.printf("[SELFTEST]   Z: avg=%+.3f std=%.4f range=[%+.3f, %+.3f] pp=%.4f g\n",
                  z_avg, z_std, z_min, z_max, z_max-z_min);
    Serial.printf("[SELFTEST]   |a|: avg=%.4f range=[%.4f, %.4f]   (静止期望 ≈ 1.000)\n",
                  m_avg, m_min, m_max);
    Serial.printf("[SELFTEST]   噪声评价: std < 0.01 g 优秀 / < 0.03 正常 / > 0.05 异常\n");
    Serial.println();

    // ------------------------------------------------------------------
    // Phase 2: BSP isGesture() 静止触发次数（应全为 0）
    // ------------------------------------------------------------------
    Serial.println("[SELFTEST] Phase 2: BSP isGesture() 静止 1s 总触发次数");
    Serial.println("[SELFTEST]   正常应全部为 0；> 0 说明 BSP gesture_task 误判");

    uint32_t start = millis();
    int cntShake=0, cntUp=0, cntDown=0, cntL=0, cntR=0, cntF=0, cntB=0;
    while (millis() - start < 1000) {
        if (k10.isGesture(::Gesture::Shake))      cntShake++;
        if (k10.isGesture(::Gesture::ScreenUp))   cntUp++;
        if (k10.isGesture(::Gesture::ScreenDown)) cntDown++;
        if (k10.isGesture(::Gesture::TiltLeft))   cntL++;
        if (k10.isGesture(::Gesture::TiltRight))  cntR++;
        if (k10.isGesture(::Gesture::TiltForward))cntF++;
        if (k10.isGesture(::Gesture::TiltBack))   cntB++;
        delay(5);
    }
    Serial.printf("[SELFTEST]   shake=%d  screenUp=%d  screenDown=%d\n",
                  cntShake, cntUp, cntDown);
    Serial.printf("[SELFTEST]   tiltL=%d tiltR=%d tiltF=%d tiltB=%d\n",
                  cntL, cntR, cntF, cntB);

    // ------------------------------------------------------------------
    // Phase 3: DeskNest GestureEngine 静止 5s 决策
    // ------------------------------------------------------------------
    Serial.println();
    Serial.println("[SELFTEST] Phase 3: DeskNest GestureEngine 静止 5s 决策统计");
    Serial.println("[SELFTEST]   正常应全部为 GESTURE_NONE；任何 > 0 都有 bug");

    g_gesture.begin();
    AccelReading acc;
    start = millis();
    int cntFD=0, cntFU=0, cntShakeEng=0, cntTap=0, cntRot=0;
    while (millis() - start < 5000) {
        // 直接读 BSP，绕过 Sensors 节流
        int xr = k10.getAccelerometerX();
        int yr = k10.getAccelerometerY();
        int zr = k10.getAccelerometerZ();
        acc.valid = true;
        acc.x = (float)xr / SC7A20H_LSB_PER_G;
        acc.y = (float)yr / SC7A20H_LSB_PER_G;
        acc.z = (float)zr / SC7A20H_LSB_PER_G;
        acc.fetchedAtMs = millis();

        GestureEvent g = g_gesture.update(acc, millis());
        if (g == GESTURE_FACE_DOWN)            cntFD++;
        else if (g == GESTURE_FACE_UP_OPEN)    cntFU++;
        else if (g == GESTURE_TAP)             cntTap++;
        else if (g == GESTURE_SHAKE_LEFT ||
                 g == GESTURE_SHAKE_RIGHT)     cntShakeEng++;
        else if (g == GESTURE_ROTATE_PORTRAIT_TO_LANDSCAPE ||
                 g == GESTURE_ROTATE_LANDSCAPE_TO_PORTRAIT) cntRot++;
        delay(20);
    }
    Serial.printf("[SELFTEST]   faceDown=%d  faceUp=%d  shake=%d  tap=%d  rotate=%d\n",
                  cntFD, cntFU, cntShakeEng, cntTap, cntRot);

    Serial.println();
    Serial.println("[SELFTEST] === 自测结束，恢复正常 loop ===");
    Serial.println();
}

} // namespace desknest