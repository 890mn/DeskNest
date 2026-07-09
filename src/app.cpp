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
#include "buttons.h"
#include "ai_usage_module.h"
#include "boot_splash.h"

#include <Arduino.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace desknest {

// 必须放在命名空间作用域（不能在 anonymous namespace 内），
// extern "C" 的 dn_app_setup / dn_app_loop 在文件作用域，看不到匿名 namespace 的成员。
uint32_t g_last_heartbeat_ms = 0;
uint32_t g_loop_count = 0;

namespace {

constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;

enum BootInitPhase : uint8_t {
    BOOT_INIT_SENSORS = 0,
    BOOT_INIT_TUNING,
    BOOT_INIT_START_REMOTE,
    BOOT_INIT_WAIT_REMOTE,
    BOOT_INIT_DONE,
    BOOT_INIT_FAILED,
};

BootInitPhase g_boot_phase = BOOT_INIT_SENSORS;
bool g_boot_k10_ready = false;
TaskHandle_t g_boot_remote_task = nullptr;

struct BootRemoteStatus {
    bool started = false;
    bool running = false;
    bool wifiReady = false;
    bool timeReady = false;
    bool aiReady = false;
    bool ready = false;
    bool failed = false;
    BootFailureReason failureReason = BOOT_FAIL_NONE;
    uint32_t startedAtMs = 0;
};

BootRemoteStatus g_boot_remote = {};
constexpr uint32_t BOOT_REMOTE_TIMEOUT_MS = 6000;

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
        case PAGE_PORTRAIT_MENU:        return "P_MENU";
        case PAGE_PORTRAIT_ENVIRONMENT: return "P_ENV";
        case PAGE_PORTRAIT_SETTINGS:    return "P_SET";
        case PAGE_LANDSCAPE_OVERVIEW:   return "L_OVR";
        case PAGE_LANDSCAPE_FOCUS:      return "L_FOC";
        case PAGE_LANDSCAPE_CUSTOM:     return "L_CUS";
        case PAGE_SLEEP_FACE_DOWN:      return "SLEEP";
        case PAGE_CONFIG_PORTAL:        return "CFG";
        case PAGE_BOOT_FAILURE:         return "BOOT_ERR";
    }
    return "?";
}

void print_banner() {
    Serial.printf("  栖屏 DeskNest v" DESKNEST_VERSION);
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
    Serial.println();
}

void boot_remote_task(void*) {
    Serial.println("[D][BOOT] remote task begin");
    g_boot_remote.running = true;
    g_boot_remote.started = true;
    g_boot_remote.startedAtMs = millis();

    dn_ai_usage_service_begin();

    while (true) {
        dn_ai_usage_service_tick();
        const AIWiFiStatus wifi = dn_ai_usage_wifi_status();
        const bool wifi_ready = wifi.state == AI_WIFI_CONNECTED;
        const bool time_ready = dn_ai_usage_time_ready();
        bool ai_ready = dn_ai_usage_live_data_ready();

        if (wifi_ready && !ai_ready) {
            const AIUsageStatus ai = dn_ai_usage_status();
            ai_ready = dn_ai_usage_live_data_ready() && !ai.fromCache;
        }

        g_boot_remote.wifiReady = wifi_ready;
        g_boot_remote.timeReady = time_ready;
        g_boot_remote.aiReady = ai_ready;
        g_boot_remote.ready = wifi_ready && time_ready && ai_ready;

        if (g_boot_remote.ready) {
            Serial.println("[D][BOOT] remote task ready");
            g_boot_remote.running = false;
            vTaskDelete(nullptr);
            return;
        }

        const uint32_t elapsed = millis() - g_boot_remote.startedAtMs;
        if (elapsed >= BOOT_REMOTE_TIMEOUT_MS) {
            g_boot_remote.failed = true;
            if (!wifi_ready) g_boot_remote.failureReason = BOOT_FAIL_WIFI;
            else if (!time_ready) g_boot_remote.failureReason = BOOT_FAIL_TIME;
            else g_boot_remote.failureReason = BOOT_FAIL_AI;
            Serial.printf("[D][BOOT] remote task timeout=%lums reason=%d\n",
                          (unsigned long)elapsed, (int)g_boot_remote.failureReason);
            g_boot_remote.running = false;
            vTaskDelete(nullptr);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void boot_step(uint32_t now) {
    switch (g_boot_phase) {
        case BOOT_INIT_SENSORS:
            Serial.println("[D][BOOT] step sensors");
            g_sensors.begin();
            g_boot_phase = BOOT_INIT_TUNING;
            break;
        case BOOT_INIT_TUNING:
            Serial.println("[D][BOOT] step tuning repl");
            dn_tuning_setup();
            g_boot_k10_ready = true;
            g_boot_phase = BOOT_INIT_START_REMOTE;
            break;
        case BOOT_INIT_START_REMOTE:
            if (!g_boot_remote.started) {
                Serial.println("[D][BOOT] step remote task");
                g_boot_remote = {};
                xTaskCreatePinnedToCore(boot_remote_task,
                                        "dn_boot_remote",
                                        6144,
                                        nullptr,
                                        1,
                                        &g_boot_remote_task,
                                        0);
            }
            g_boot_phase = BOOT_INIT_WAIT_REMOTE;
            break;
        case BOOT_INIT_WAIT_REMOTE: {
            dn_boot_splash_update(now,
                                  g_boot_k10_ready,
                                  g_boot_remote.wifiReady,
                                  g_boot_remote.timeReady,
                                  g_boot_remote.aiReady,
                                  g_boot_remote.failed,
                                  g_boot_remote.failureReason);
            if (g_boot_remote.ready && !dn_boot_splash_active()) {
                g_boot_phase = BOOT_INIT_DONE;
            } else if (g_boot_remote.failed) {
                g_boot_phase = BOOT_INIT_FAILED;
            }
            return;
        }
        case BOOT_INIT_FAILED:
        case BOOT_INIT_DONE:
        default:
            return;
    }

    dn_boot_splash_update(now, g_boot_k10_ready, false, false, false);
}

} // namespace
} // namespace desknest

void dn_app_setup() {
    Serial.begin(115200);
    delay(200);

    desknest::print_banner();
    Serial.println("[D][BOOT] entering SYSTEM_BOOT");
    Serial.println("[D][BOOT] P0-C: initializing gesture + state machine...");
    desknest::g_gesture.begin();
    desknest::g_state.begin();
    desknest::g_boot_phase = desknest::BOOT_INIT_SENSORS;
    desknest::g_boot_k10_ready = false;
    desknest::g_boot_remote = {};
    desknest::g_boot_remote_task = nullptr;

    Serial.println("[D][BOOT] P0-B0: bootstrap K10 BSP...");
    desknest::dn_k10_bootstrap();
    desknest::g_boot_k10_ready = desknest::dn_k10_bootstrap_ready();

    desknest::dn_boot_splash_begin(millis());

    Serial.println("[D][BOOT] P0-D: initializing UI...");
    dn_ui_setup();

    Serial.println("[D][BOOT] done. entering main loop.");

    desknest::g_last_heartbeat_ms = millis();
}

void dn_app_loop() {
    using namespace desknest;
    g_loop_count++;

    const uint32_t now = millis();
    if (g_state.snapshot().system == SYSTEM_BOOT || dn_boot_splash_active()) {
        boot_step(now);
        dn_ui_render();
        if (g_boot_phase == BOOT_INIT_DONE && g_state.snapshot().system == SYSTEM_BOOT) {
            g_state.forceSystem(SYSTEM_ACTIVE);
            g_state.forcePage(PAGE_PORTRAIT_OVERVIEW);
            g_state.notifyInput();
            Serial.println("[D][BOOT] init complete -> ACTIVE");
        } else if (g_boot_phase == BOOT_INIT_FAILED && g_state.snapshot().system == SYSTEM_BOOT) {
            g_state.forceSystem(SYSTEM_ACTIVE);
            g_state.forcePage(PAGE_BOOT_FAILURE);
            g_state.notifyInput();
            Serial.println("[D][BOOT] init failed -> BOOT_FAILURE");
        }
        return;
    }

    if (g_state.snapshot().page == PAGE_BOOT_FAILURE) {
        dn_ui_render();
        return;
    }

    // 1) 传感器
    g_sensors.update();
    AccelReading acc = g_sensors.accel();

    // 2) 手势（tuning REPL 可能用 pending feed 覆盖真实 accel 一帧）
    dn_tuning_take_feed(acc);   // 若有 pending feed 替换 acc；无则保留原值
    const GestureEvent g = g_gesture.update(acc, now);

    // 2.5) tuning 后处理：吃串口、recording 日志
    dn_tuning_post_step(now, acc, g);

    // 3) 按键 —— 走 K10 BSP 的 buttonA / buttonB / buttonAB
    //    （BSP 在内部用 FreeRTOS 任务轮询 GPIO，引脚由 BSP 决定）
    const ButtonEvent b = dn_button_poll(now);
    if (b != BUTTON_NONE) {
        Serial.printf("[D][INPUT] button=%d\n", (int)b);
    }

    // 4) 状态机
    dn_ai_usage_service_tick();
    const OrientationState detected = g_gesture.orientation();
    g_state.update(g, b, detected, now);

    // 5) UI 渲染（按页面变化或 1Hz 节流，内部做）
    dn_ui_render();

    // 6) 心跳 + 传感器 + 状态 —— 默认安静（g_tuning.verbose = 0），
    //    调参时终端不被打扰。串口 REPL 'verbose 1' 开
    if (g_tuning.verbose && (now - g_last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS)) {
        g_last_heartbeat_ms = now;
        print_heartbeat(g_state.snapshot());
        print_sensors();
    }

    // P0-E 接入 power（包含翻面栖息省电）
}
