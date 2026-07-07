// src/app.cpp
// 栖屏 DeskNest - 顶层 setup/loop
// "栖于桌面，息于常亮之间"
//
// P0-A 工程骨架：仅做初始化 + 串口心跳。
// P0-B 接入 sensors：心跳同时打印温湿/光/电池/加速度。
// 后续 milestone 会逐步接入 state_machine / ui / power / network。

#include "app.h"
#include "config.h"
#include "sensors.h"
#include "gesture.h"
#include "gesture_tuning.h"
#include "state_machine.h"
#include "ui.h"

#include <Arduino.h>

namespace desknest {

// 必须放在命名空间作用域（不能在 anonymous namespace 内），
// extern "C" 的 dn_app_setup / dn_app_loop 在文件作用域，看不到匿名 namespace 的成员。
uint32_t g_last_heartbeat_ms = 0;
uint32_t g_loop_count = 0;

namespace {

constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;

const char* systemStateName(SystemState s) {
    switch (s) {
        case SYSTEM_BOOT:            return "BOOT";
        case SYSTEM_ACTIVE:          return "ACTIVE";
        case SYSTEM_AMBIENT:         return "AMBIENT";
        case SYSTEM_LIGHT_SLEEP:     return "LIGHT_SLEEP";
        case SYSTEM_FACE_DOWN_SLEEP: return "FACE_DOWN_SLEEP";
        case SYSTEM_CONFIG:          return "CONFIG";
    }
    return "?";
}

const char* orientationName(OrientationState o) {
    switch (o) {
        case ORIENTATION_UNKNOWN:   return "UNK";
        case ORIENTATION_PORTRAIT:  return "PORT";
        case ORIENTATION_LANDSCAPE: return "LAND";
        case ORIENTATION_FACE_DOWN: return "FACE_DN";
    }
    return "?";
}

const char* pageName(UIPage p) {
    switch (p) {
        case PAGE_PORTRAIT_OVERVIEW:    return "P_OVR";
        case PAGE_PORTRAIT_AI_USAGE:    return "P_AI";
        case PAGE_PORTRAIT_ENVIRONMENT: return "P_ENV";
        case PAGE_PORTRAIT_SETTINGS:    return "P_SET";
        case PAGE_LANDSCAPE_OVERVIEW:   return "L_OVR";
        case PAGE_LANDSCAPE_FOCUS:      return "L_FOC";
        case PAGE_LANDSCAPE_CUSTOM:     return "L_CUS";
        case PAGE_SLEEP_FACE_DOWN:      return "SLEEP";
        case PAGE_CONFIG_PORTAL:        return "CFG";
    }
    return "?";
}

void print_banner() {
    Serial.println();
    Serial.println("=========================================");
    Serial.printf("  栖屏 DeskNest v" DESKNEST_VERSION);
    Serial.println("  Perched on desk, dormant between wake-ups.");
    Serial.println("=========================================");
    Serial.println();
}

void print_heartbeat(const StateSnapshot& s) {
    Serial.print("[D][STATE] sys=");
    Serial.print(systemStateName(s.system));
    Serial.print(" orient=");
    Serial.print(orientationName(s.orientation));
    Serial.print(" page=");
    Serial.print(pageName(s.page));
    Serial.print(" rotLock=");
    Serial.print((int)s.rotLock);
    Serial.print(" idle=");
    Serial.print((millis() - s.lastInputMs) / 1000);
    Serial.println("s");
}

void print_sensors() {
    const auto aht  = g_sensors.aht20();
    const auto lux  = g_sensors.ltr303();
    const auto acc  = g_sensors.accel();
    const auto bat  = g_sensors.battery();

    Serial.print("[D][SENS] ");
    if (aht.valid) {
        Serial.print("T=");
        Serial.print(aht.temperatureC, 1);
        Serial.print("C  H=");
        Serial.print(aht.humidityPct, 1);
        Serial.print("%");
    } else {
        Serial.print("T=NA H=NA");
    }

    Serial.print("  Lux=");
    Serial.print(lux.valid ? String(lux.lux, 0) : String("NA"));

    Serial.print("  ACC(");
    if (acc.valid) {
        Serial.print(acc.x, 2); Serial.print(",");
        Serial.print(acc.y, 2); Serial.print(",");
        Serial.print(acc.z, 2);
    } else {
        Serial.print("NA");
    }
    Serial.print(")");

    Serial.print("  BAT=");
    if (bat.valid) {
        Serial.print(bat.percent);
        Serial.print("% ");
        Serial.print(bat.voltage, 2);
        Serial.print("V");
        if (bat.charging) Serial.print(" CHG");
    } else {
        Serial.print("NA");
    }
    Serial.println();
}

} // namespace
} // namespace desknest

void dn_app_setup() {
    Serial.begin(115200);
    delay(200);

    desknest::print_banner();
    Serial.println("[D][BOOT] entering SYSTEM_BOOT");
    Serial.println("[D][BOOT] P0-B: initializing sensors...");
    desknest::g_sensors.begin();

    // P0 阶段：跑 3 轴传感器自测，定位"不稳定"问题
    // 完成后才继续 gesture / state / UI 初始化
    desknest::g_sensors.selfTest(10000);

    Serial.println("[D][BOOT] P0-C: initializing gesture + state machine...");
    desknest::g_gesture.begin();
    desknest::g_state.begin();

    Serial.println("[D][BOOT] P0-D: initializing UI...");
    dn_ui_setup();

    Serial.println("[D][BOOT] P0-E: initializing gesture tuning REPL...");
    desknest::dn_tuning_setup();

    Serial.println("[D][BOOT] done. entering main loop.");

    desknest::g_last_heartbeat_ms = millis();
}

void dn_app_loop() {
    using namespace desknest;
    g_loop_count++;

    // 1) 传感器
    g_sensors.update();
    AccelReading acc = g_sensors.accel();

    // 2) 手势（tuning REPL 可能用 pending feed 覆盖真实 accel 一帧）
    const uint32_t now = millis();
    dn_tuning_take_feed(acc);   // 若有 pending feed 替换 acc；无则保留原值
    const GestureEvent g = g_gesture.update(acc, now);

    // 2.5) tuning 后处理：吃串口、recording 日志
    dn_tuning_post_step(now, acc, g);

    // 3) 按键（P0-C 阶段先用 mock 触发器：每 10s 模拟 BUTTON_NEXT 一次方便看状态转移）
    static uint32_t last_mock_btn_ms = 0;
    static uint8_t mock_btn_idx = 0;
    ButtonEvent b = BUTTON_NONE;
    if (now - last_mock_btn_ms >= 10000) {
        last_mock_btn_ms = now;
        // 模拟 5 种按键轮流
        const ButtonEvent seq[] = {BUTTON_NEXT, BUTTON_NEXT, BUTTON_PREV, BUTTON_MENU, BUTTON_BACK};
        b = seq[mock_btn_idx % 5];
        mock_btn_idx++;
        Serial.printf("[D][INPUT] mock button=%d\n", (int)b);
    }

    // 4) 状态机
    const OrientationState detected = g_gesture.orientation();
    g_state.update(g, b, detected, now);

    // 5) UI 渲染（按页面变化或 1Hz 节流，内部做）
    dn_ui_render();

    // 6) 心跳 + 传感器 + 状态
    if (now - g_last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
        g_last_heartbeat_ms = now;
        print_heartbeat(g_state.snapshot());
        print_sensors();
    }

    // P0-E 接入 power（包含翻面栖息省电）
}
