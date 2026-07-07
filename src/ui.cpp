// src/ui.cpp
// 栖屏 DeskNest - UI 渲染实现（P0-D 简化版）
// "栖于桌面，息于常亮之间"
//
// 设计原则：
//   - 每页开头 canvasClear() 一次清空；之后所有 draw 用 autoClean=false
//     （避免 X/Y 版 canvasText 的 autoClean 把后续区域填黑覆盖前面内容）
//   - 主体用 24px 英文（GB2312 芯片字体覆盖不全，宽字易被 LVGL 截）
//   - FACE_DOWN 显示英文 slogan 三行居中
//   - RGB 默认全灭，避免干扰；后续 P1 再按状态点亮

#include "ui.h"
#include "config.h"
#include "sensors.h"
#include "state_machine.h"

#include <Arduino.h>
#include <stdio.h>
#include <unihiker_k10.h>

namespace desknest {

// k10 全局实例在 sensors.cpp 定义
extern UNIHIKER_K10 k10;

namespace {

// ---------------------------------------------------------------------------
// 颜色
// ---------------------------------------------------------------------------
constexpr uint32_t COLOR_BG       = 0x000000;
constexpr uint32_t COLOR_TITLE    = 0xFFFFFF;
constexpr uint32_t COLOR_DIM      = 0x808080;
constexpr uint32_t COLOR_NORMAL   = 0x00FF00;
constexpr uint32_t COLOR_WARN     = 0xFF8000;
constexpr uint32_t COLOR_ALERT    = 0xFF0000;
constexpr uint32_t COLOR_ACCENT   = 0x00BFFF;

constexpr uint32_t RGB_OFF        = 0x000000;

// 字体（短文本用 24，长文本用 16）
constexpr auto F24 = Canvas::eCNAndENFont24;
constexpr auto F16 = Canvas::eCNAndENFont16;

// ---------------------------------------------------------------------------
// Mock AI 用量
// ---------------------------------------------------------------------------
struct AiUsage { int chatgpt_pct; int claude_pct; };
AiUsage mock_ai_usage(uint32_t now_ms) {
    AiUsage u;
    int base = (now_ms / 60000) % 80 + 10;
    u.chatgpt_pct = base + ((now_ms / 7000) % 15);
    u.claude_pct  = (base / 2) + ((now_ms / 11000) % 20);
    if (u.chatgpt_pct > 99) u.chatgpt_pct = 99;
    if (u.claude_pct  > 99) u.claude_pct  = 99;
    if (u.chatgpt_pct < 0)   u.chatgpt_pct = 0;
    if (u.claude_pct  < 0)   u.claude_pct  = 0;
    return u;
}

uint32_t bar_color(int pct) {
    if (pct > 85) return COLOR_ALERT;
    if (pct > 50) return COLOR_WARN;
    return COLOR_NORMAL;
}

const char* orient_str(OrientationState o) {
    switch (o) {
        case ORIENTATION_PORTRAIT:  return "Portrait";
        case ORIENTATION_LANDSCAPE: return "Landscape";
        case ORIENTATION_FACE_DOWN: return "Face-Down";
        default:                    return "Unknown";
    }
}

const char* sys_str(SystemState s) {
    switch (s) {
        case SYSTEM_ACTIVE:          return "ACTIVE";
        case SYSTEM_AMBIENT:         return "AMBIENT";
        case SYSTEM_LIGHT_SLEEP:     return "SLEEP";
        case SYSTEM_FACE_DOWN_SLEEP: return "FACE_DN";
        case SYSTEM_CONFIG:          return "CONFIG";
        case SYSTEM_BOOT:            return "BOOT";
        default:                     return "?";
    }
}

// ---------------------------------------------------------------------------
// 通用工具：行绘制（autoClean=false，不重复清屏）
// ---------------------------------------------------------------------------
void txt(int x, int y, const char* s, uint32_t color, Canvas::eFontSize_t font) {
    k10.canvas->canvasText(s, x, y, color, font, 50, false);
}

void line_h(int x1, int y, int x2, uint32_t color) {
    k10.canvas->canvasLine(x1, y, x2, y, color);
}

void rect_filled(int x, int y, int w, int h, uint32_t color) {
    k10.canvas->canvasRectangle(x, y, w, h, color, color, true);
}

void progress_bar(int x, int y, int w, int h, int pct, uint32_t bar_c) {
    rect_filled(x, y, w, h, COLOR_DIM);
    int bar_w = (w * pct) / 100;
    if (bar_w > 0) rect_filled(x, y, bar_w, h, bar_c);
}

void draw_status_bar() {
    const auto& s = g_state.snapshot();
    char buf[40];
    uint32_t idle_s = (millis() - s.lastInputMs) / 1000;
    snprintf(buf, sizeof(buf), "[%s] idle=%lus", sys_str(s.system),
             (unsigned long)idle_s);
    txt(5, 33, buf, COLOR_DIM, F16);
}

// ---------------------------------------------------------------------------
// 竖屏页面
// ---------------------------------------------------------------------------
void render_portrait_overview() {
    k10.canvas->canvasClear();

    txt(5, 5, "DeskNest", COLOR_TITLE, F24);
    draw_status_bar();
    line_h(5, 55, 235, COLOR_DIM);

    const auto& s = g_state.snapshot();
    const auto aht = g_sensors.aht20();
    const auto lux = g_sensors.ltr303();
    const auto bat = g_sensors.battery();

    char buf[40];
    int y = 65;
    constexpr int LH = 30;

    if (aht.valid) {
        snprintf(buf, sizeof(buf), "Temp   %.1f C", aht.temperatureC);
        txt(5, y, buf, COLOR_NORMAL, F24); y += LH;
        snprintf(buf, sizeof(buf), "Humid  %.1f %%", aht.humidityPct);
        txt(5, y, buf, COLOR_ACCENT, F24); y += LH;
    } else {
        txt(5, y, "Temp   --", COLOR_DIM, F24); y += LH;
        txt(5, y, "Humid  --", COLOR_DIM, F24); y += LH;
    }

    snprintf(buf, sizeof(buf), "Light  %d lx", (int)lux.lux);
    txt(5, y, buf, COLOR_ACCENT, F24); y += LH;

    snprintf(buf, sizeof(buf), "Orient %s", orient_str(s.orientation));
    txt(5, y, buf, COLOR_TITLE, F24); y += LH;

    if (bat.valid) {
        snprintf(buf, sizeof(buf), "Batt   %d%% %.2fV", bat.percent, bat.voltage);
        txt(5, y, buf, COLOR_DIM, F24);
    } else {
        txt(5, y, "Batt   USB (N/A)", COLOR_DIM, F24);
    }

    txt(5, 295, "[A] Next  [B] Prev", COLOR_DIM, F16);
}

void render_portrait_ai_usage() {
    k10.canvas->canvasClear();

    txt(5, 5, "AI Usage", COLOR_TITLE, F24);
    draw_status_bar();
    line_h(5, 55, 235, COLOR_DIM);

    AiUsage u = mock_ai_usage(millis());
    char buf[20];

    txt(5, 70, "ChatGPT Plus", COLOR_TITLE, F24);
    progress_bar(5, 100, 200, 18, u.chatgpt_pct, bar_color(u.chatgpt_pct));
    snprintf(buf, sizeof(buf), "%d%%", u.chatgpt_pct);
    txt(210, 102, buf, COLOR_TITLE, F16);

    txt(5, 135, "Claude Pro", COLOR_TITLE, F24);
    progress_bar(5, 165, 200, 18, u.claude_pct, bar_color(u.claude_pct));
    snprintf(buf, sizeof(buf), "%d%%", u.claude_pct);
    txt(210, 167, buf, COLOR_TITLE, F16);

    txt(5, 220, "(Mock data)", COLOR_DIM, F16);
    txt(5, 245, "P1: cc-switch API", COLOR_DIM, F16);
    txt(5, 295, "[A] Next", COLOR_DIM, F16);
}

void render_portrait_environment() {
    k10.canvas->canvasClear();

    txt(5, 5, "Environment", COLOR_TITLE, F24);
    draw_status_bar();
    line_h(5, 55, 235, COLOR_DIM);

    const auto aht = g_sensors.aht20();
    const auto lux = g_sensors.ltr303();
    char buf[40];
    int y = 70;

    if (aht.valid) {
        snprintf(buf, sizeof(buf), "%.1f", aht.temperatureC);
        txt(20, y, buf, COLOR_NORMAL, F24);
        txt(120, y, "C", COLOR_DIM, F24);
    } else {
        txt(20, y, "--", COLOR_DIM, F24);
    }
    y += 45;

    if (aht.valid) {
        snprintf(buf, sizeof(buf), "Humid %.1f %%", aht.humidityPct);
        txt(5, y, buf, COLOR_ACCENT, F24); y += 32;
    }

    snprintf(buf, sizeof(buf), "Light %d lx", (int)lux.lux);
    txt(5, y, buf, COLOR_ACCENT, F24); y += 32;

    if (aht.valid) {
        const char* comfort = "Normal";
        uint32_t c = COLOR_WARN;
        if (aht.temperatureC > 18 && aht.temperatureC < 28 &&
            aht.humidityPct > 30 && aht.humidityPct < 70) {
            comfort = "Comfort";
            c = COLOR_NORMAL;
        }
        snprintf(buf, sizeof(buf), "Feel: %s", comfort);
        txt(5, y, buf, c, F24);
    }

    txt(5, 295, "[A] Next", COLOR_DIM, F16);
}

void render_portrait_settings() {
    k10.canvas->canvasClear();

    txt(5, 5, "Settings", COLOR_TITLE, F24);
    draw_status_bar();
    line_h(5, 55, 235, COLOR_DIM);

    int y = 65;
    txt(5, y, "Power   Balanced", COLOR_NORMAL, F24); y += 28;
    txt(5, y, "  30s / 90s", COLOR_DIM, F16); y += 28;
    txt(5, y, "Sync    Battery", COLOR_NORMAL, F24); y += 28;
    txt(5, y, "  22min / 60min", COLOR_DIM, F16); y += 28;
    txt(5, y, "Density Normal", COLOR_NORMAL, F24); y += 28;
    txt(5, y, "Rotate  Auto", COLOR_NORMAL, F24); y += 28;
    txt(5, y, "Theme   Dark", COLOR_NORMAL, F24);

    txt(5, 295, "[A+B] Factory reset", COLOR_ALERT, F16);
}

// ---------------------------------------------------------------------------
// 横屏页面（简版，仍在 240x320 canvas 上画）
// ---------------------------------------------------------------------------
void render_landscape_overview() {
    k10.canvas->canvasClear();
    txt(5, 5, "DeskNest", COLOR_TITLE, F24);
    draw_status_bar();

    const auto aht = g_sensors.aht20();
    char buf[40];
    if (aht.valid) {
        snprintf(buf, sizeof(buf), "%.1fC %.1f%%",
                 aht.temperatureC, aht.humidityPct);
        txt(5, 60, buf, COLOR_NORMAL, F24);
    }
    txt(5, 295, "[A] Next", COLOR_DIM, F16);
}

void render_landscape_focus() {
    k10.canvas->canvasClear();
    txt(5, 5, "AI Focus", COLOR_TITLE, F24);
    draw_status_bar();

    AiUsage u = mock_ai_usage(millis());
    char buf[20];
    snprintf(buf, sizeof(buf), "ChatGPT %d%%", u.chatgpt_pct);
    txt(5, 60, buf, COLOR_NORMAL, F24);
    progress_bar(5, 90, 200, 16, u.chatgpt_pct, bar_color(u.chatgpt_pct));
    snprintf(buf, sizeof(buf), "Claude %d%%", u.claude_pct);
    txt(5, 120, buf, COLOR_NORMAL, F24);
    progress_bar(5, 150, 200, 16, u.claude_pct, bar_color(u.claude_pct));

    txt(5, 295, "[A] Next", COLOR_DIM, F16);
}

void render_landscape_custom() {
    k10.canvas->canvasClear();
    txt(5, 5, "Custom", COLOR_TITLE, F24);
    draw_status_bar();
    txt(5, 120, "(P1 user config)", COLOR_DIM, F24);
    txt(5, 295, "[A] Next", COLOR_DIM, F16);
}

// ---------------------------------------------------------------------------
// 特殊页
// ---------------------------------------------------------------------------
void render_face_down() {
    k10.canvas->canvasClear();
    // 三行 slogan 居中（短行用 24px）
    txt(40, 120, "Perched on desk,", COLOR_DIM, F24);
    txt(20, 150, "dormant between", COLOR_DIM, F24);
    txt(70, 180, "wake-ups.", COLOR_DIM, F24);
}

void render_config_portal() {
    k10.canvas->canvasClear();
    txt(5, 5, "WiFi Setup", COLOR_TITLE, F24);
    draw_status_bar();
    line_h(5, 55, 235, COLOR_DIM);

    txt(5, 80, "Connect to:", COLOR_TITLE, F24);
    txt(5, 110, "DeskNest-XXXX", COLOR_ACCENT, F24);
    txt(5, 160, "Open browser:", COLOR_TITLE, F24);
    txt(5, 190, "192.168.4.1", COLOR_ACCENT, F24);
}

// ---------------------------------------------------------------------------
// 调度 + RGB
// ---------------------------------------------------------------------------
UIPage   g_last_page = PAGE_COUNT;
uint32_t g_last_ms   = 0;

void render_dispatch() {
    const UIPage p = g_state.snapshot().page;
    switch (p) {
        case PAGE_PORTRAIT_OVERVIEW:    render_portrait_overview();   break;
        case PAGE_PORTRAIT_AI_USAGE:    render_portrait_ai_usage();   break;
        case PAGE_PORTRAIT_ENVIRONMENT: render_portrait_environment();break;
        case PAGE_PORTRAIT_SETTINGS:    render_portrait_settings();   break;
        case PAGE_LANDSCAPE_OVERVIEW:   render_landscape_overview();  break;
        case PAGE_LANDSCAPE_FOCUS:      render_landscape_focus();     break;
        case PAGE_LANDSCAPE_CUSTOM:     render_landscape_custom();    break;
        case PAGE_SLEEP_FACE_DOWN:      render_face_down();           break;
        case PAGE_CONFIG_PORTAL:        render_config_portal();       break;
        default: break;
    }
    // 关键：lv_task_handler 才会把 canvas buffer 推到 TFT。否则屏全黑。
    k10.canvas->updateCanvas();
    g_last_page = p;
    g_last_ms   = millis();
}

// RGB 全灭（P0-D 简化，避免干扰）。后续 P1 再按状态点亮。
void rgb_off() {
    static bool done = false;
    if (done) return;
    done = true;
    k10.rgb->write(-1, RGB_OFF);
}

}  // namespace
}  // namespace desknest

// ============================================================================
// 对外 API
// ============================================================================

void dn_ui_setup() {
    using namespace desknest;
    Serial.println("[D][UI] init screen...");
    k10.initScreen(2);
    k10.creatCanvas();
    k10.setScreenBackground(COLOR_BG);
    k10.canvas->canvasClear();
    k10.canvas->updateCanvas();
    Serial.println("[D][UI] screen ready (dir=2, canvas 240x320)");

    k10.rgb->write(-1, RGB_OFF);
}

void dn_ui_render() {
    using namespace desknest;
    const uint32_t now = millis();
    const UIPage cur = g_state.snapshot().page;

    const bool page_changed = (cur != g_last_page);
    const bool due = (now - g_last_ms >= 1000);

    if (page_changed || due) {
        render_dispatch();
    }
    rgb_off();
}