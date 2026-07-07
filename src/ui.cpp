// src/ui.cpp
// 栖屏 DeskNest - UI 渲染层（脏区局部刷新版）
// "栖于桌面，息于常亮之间"
//
// 之前每页开头 canvasClear() 全屏清黑再全画一遍，1Hz 自动重绘导致闪屏。
// 改用「每页一个 PageCache + drawField 字段级局部更新」：
//   - 翻页（page change）才 canvasClear + 全画
//   - 同页每帧用 cache 比对字段值，没变就不动；变了用黑矩形擦一块再写
//   - 1Hz 心跳拿掉；status bar 的 idle=Xs 因为秒数真的变了，自然触发字段更新
//   - 字段没变 → updateCanvas() 不调，SPI 不传输

#include "ui.h"
#include "config.h"
#include "sensors.h"
#include "state_machine.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <unihiker_k10.h>

namespace desknest {

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

constexpr auto F24 = Canvas::eCNAndENFont24;
constexpr auto F16 = Canvas::eCNAndENFont16;

// ---------------------------------------------------------------------------
// PageCache：每页一份；drawField 拿它对比 + 写入
// ---------------------------------------------------------------------------
struct PageCache {
    bool    ever_drawn  = false;
    char    title[20]   = "";
    char    status[40]  = "";
    char    temp[24]    = "";
    char    humid[24]   = "";
    char    light[20]   = "";
    char    orient[20]  = "";
    char    batt[28]    = "";
    char    comfort[24] = "";
    char    hint[40]    = "";
    char    subline[40] = "";
    char    bigtemp[16] = "";
    char    bigunit[8]  = "";
    int     chatgpt_pct    = -1;
    int     claude_pct     = -1;
    int     chatgpt_bar_w  = -1;
    int     claude_bar_w   = -1;
    int     power_idx      = -1;
    int     sync_idx       = -1;
    int     density_idx    = -1;
    int     rotate_idx     = -1;
    int     theme_idx      = -1;
    char    ssid_line[24]  = "";
    char    url_line[24]   = "";
    // face_down 页用：三行 slogan
    char    slogan1[32] = "";
    char    slogan2[32] = "";
    char    slogan3[32] = "";
};

static PageCache g_cache[PAGE_COUNT];
static UIPage   g_last_page = PAGE_COUNT;

// 估算字宽 / 字高 —— canvas 没提供 measure 接口；估准就行
static int charW(Canvas::eFontSize_t f) { return (f == F24) ? 14 : 9; }
static int lineH(Canvas::eFontSize_t f) { return (f == F24) ? 28 : 18; }

// ---------------------------------------------------------------------------
// drawField：值变了才画。先用黑矩形擦老的一块，再写新值。
//  返回 true = 这次重画了（调用方决定要不要 updateCanvas）
// ---------------------------------------------------------------------------
static bool drawField(char* cache, size_t cap,
                      const char* new_val,
                      int x, int y,
                      int max_chars,
                      Canvas::eFontSize_t font, uint32_t color) {
    if (strcmp(cache, new_val) == 0) return false;
    int w = max_chars * charW(font);
    int h = lineH(font);
    k10.canvas->canvasRectangle(x, y, w, h, COLOR_BG, COLOR_BG, true);
    k10.canvas->canvasText(new_val, x, y, color, font, 50, false);
    strncpy(cache, new_val, cap - 1);
    cache[cap - 1] = '\0';
    return true;
}

// 进度条：值变了才画
static bool drawBar(int* cache_w, int new_pct,
                    int x, int y, int w, int h, uint32_t bar_c) {
    int new_w = (w * new_pct) / 100;
    if (new_w < 0) new_w = 0;
    if (*cache_w == new_w) return false;
    // 整条槽先擦灰
    k10.canvas->canvasRectangle(x, y, w, h, COLOR_DIM, COLOR_DIM, true);
    if (new_w > 0) {
        k10.canvas->canvasRectangle(x, y, new_w, h, bar_c, bar_c, true);
    }
    *cache_w = new_w;
    return true;
}

// 静态一次性画（只在 ever_drawn=false 时画）
static void drawOnce(bool* flag,
                     int x, int y, const char* s, uint32_t color, Canvas::eFontSize_t font) {
    if (*flag) return;
    k10.canvas->canvasText(s, x, y, color, font, 50, false);
    *flag = true;
}
static void drawOnceLine(bool* flag, int x1, int y, int x2, uint32_t color) {
    if (*flag) return;
    k10.canvas->canvasLine(x1, y, x2, y, color);
    *flag = true;
}
static void drawOnceRect(bool* flag, int x, int y, int w, int h, uint32_t color) {
    if (*flag) return;
    k10.canvas->canvasRectangle(x, y, w, h, color, color, true);
    *flag = true;
}

// 文字串：orient / sys 名
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

// Mock AI 用量
struct AiUsage { int chatgpt_pct; int claude_pct; };
AiUsage mock_ai_usage(uint32_t now_ms) {
    AiUsage u;
    int base = (now_ms / 60000) % 80 + 10;
    u.chatgpt_pct = base + ((now_ms / 7000) % 15);
    u.claude_pct  = (base / 2) + ((now_ms / 11000) % 20);
    if (u.chatgpt_pct > 99) u.chatgpt_pct = 99;
    if (u.claude_pct  > 99) u.claude_pct  = 99;
    return u;
}
uint32_t bar_color(int pct) {
    if (pct > 85) return COLOR_ALERT;
    if (pct > 50) return COLOR_WARN;
    return COLOR_NORMAL;
}

// ---------------------------------------------------------------------------
// 通用：每页都画 status bar + 页码指示
// ---------------------------------------------------------------------------
static bool draw_status_bar(UIPage p) {
    PageCache& c = g_cache[p];
    const auto& s = g_state.snapshot();
    char buf[40];
    uint32_t idle_s = (millis() - s.lastInputMs) / 1000;
    snprintf(buf, sizeof(buf), "[%s] idle=%lus",
             sys_str(s.system), (unsigned long)idle_s);
    bool dirty = drawField(c.status, sizeof(c.status), buf, 5, 33, 28, F16, COLOR_DIM);

    // 页码 Pn/N —— 右上角，让用户一眼知道是第几页
    //   用 F24 字模凸显；只有 8 个 port page，值变化少，cache 命中率高
    char pbuf[8];
    snprintf(pbuf, sizeof(pbuf), "P%u/%u", (unsigned)(p + 1), (unsigned)PAGE_COUNT);
    dirty |= drawField(c.hint, sizeof(c.hint), pbuf, 198, 5, 6, F24, COLOR_ACCENT);
    return dirty;
}

// ===========================================================================
// 竖屏页面
// ===========================================================================
static bool render_portrait_overview() {
    PageCache& c = g_cache[PAGE_PORTRAIT_OVERVIEW];
    bool dirty = false;

    drawOnce(&c.ever_drawn, 5, 5, "DeskNest", COLOR_TITLE, F24);
    drawOnceLine(&c.ever_drawn, 5, 55, 235, COLOR_DIM);

    dirty |= draw_status_bar(PAGE_PORTRAIT_OVERVIEW);

    const auto& s = g_state.snapshot();
    const auto aht = g_sensors.aht20();
    const auto lux = g_sensors.ltr303();
    const auto bat = g_sensors.battery();
    char buf[40];

    if (aht.valid) {
        snprintf(buf, sizeof(buf), "Temp   %.1f C", aht.temperatureC);
        dirty |= drawField(c.temp, sizeof(c.temp), buf, 5, 65, 18, F24, COLOR_NORMAL);
        snprintf(buf, sizeof(buf), "Humid  %.1f %%", aht.humidityPct);
        dirty |= drawField(c.humid, sizeof(c.humid), buf, 5, 95, 18, F24, COLOR_ACCENT);
    } else {
        dirty |= drawField(c.temp,  sizeof(c.temp),  "Temp   --",      5, 65, 18, F24, COLOR_DIM);
        dirty |= drawField(c.humid, sizeof(c.humid), "Humid  --",      5, 95, 18, F24, COLOR_DIM);
    }

    snprintf(buf, sizeof(buf), "Light  %d lx", (int)lux.lux);
    dirty |= drawField(c.light, sizeof(c.light), buf, 5, 125, 18, F24, COLOR_ACCENT);

    snprintf(buf, sizeof(buf), "Orient %s", orient_str(s.orientation));
    dirty |= drawField(c.orient, sizeof(c.orient), buf, 5, 155, 18, F24, COLOR_TITLE);

    if (bat.valid) {
        snprintf(buf, sizeof(buf), "Batt   %d%% %.2fV", bat.percent, bat.voltage);
        dirty |= drawField(c.batt, sizeof(c.batt), buf, 5, 185, 22, F24, COLOR_DIM);
    } else {
        dirty |= drawField(c.batt, sizeof(c.batt), "Batt   USB (N/A)", 5, 185, 22, F24, COLOR_DIM);
    }

    dirty |= drawField(c.hint, sizeof(c.hint), "[A] Next  [B] Prev",
                       5, 295, 22, F16, COLOR_DIM);
    return dirty;
}

static bool render_portrait_ai_usage() {
    PageCache& c = g_cache[PAGE_PORTRAIT_AI_USAGE];
    bool dirty = false;

    drawOnce(&c.ever_drawn, 5, 5, "AI Usage", COLOR_TITLE, F24);
    drawOnceLine(&c.ever_drawn, 5, 55, 235, COLOR_DIM);
    dirty |= draw_status_bar(PAGE_PORTRAIT_AI_USAGE);

    AiUsage u = mock_ai_usage(millis());

    // 标题
    dirty |= drawField(c.subline, sizeof(c.subline), "ChatGPT Plus",
                       5, 70, 16, F24, COLOR_TITLE);
    // 进度条 + 百分比
    dirty |= drawBar(&c.chatgpt_bar_w, u.chatgpt_pct, 5, 100, 200, 18,
                     bar_color(u.chatgpt_pct));
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", u.chatgpt_pct);
    dirty |= drawField(c.title, sizeof(c.title), buf, 210, 102, 6, F16, COLOR_TITLE);

    dirty |= drawField(c.hint, sizeof(c.hint), "Claude Pro",
                       5, 135, 16, F24, COLOR_TITLE);
    dirty |= drawBar(&c.claude_bar_w, u.claude_pct, 5, 165, 200, 18,
                     bar_color(u.claude_pct));
    snprintf(buf, sizeof(buf), "%d%%", u.claude_pct);
    dirty |= drawField(c.temp, sizeof(c.temp), buf, 210, 167, 6, F16, COLOR_TITLE);

    dirty |= drawField(c.batt, sizeof(c.batt), "(Mock data)",      5, 220, 22, F16, COLOR_DIM);
    dirty |= drawField(c.comfort, sizeof(c.comfort), "P1: cc-switch API", 5, 245, 22, F16, COLOR_DIM);
    dirty |= drawField(c.humid, sizeof(c.humid), "[A] Next",         5, 295, 22, F16, COLOR_DIM);
    return dirty;
}

static bool render_portrait_environment() {
    PageCache& c = g_cache[PAGE_PORTRAIT_ENVIRONMENT];
    bool dirty = false;

    drawOnce(&c.ever_drawn, 5, 5, "Environment", COLOR_TITLE, F24);
    drawOnceLine(&c.ever_drawn, 5, 55, 235, COLOR_DIM);
    dirty |= draw_status_bar(PAGE_PORTRAIT_ENVIRONMENT);

    const auto aht = g_sensors.aht20();
    const auto lux = g_sensors.ltr303();
    char buf[40];

    if (aht.valid) {
        snprintf(buf, sizeof(buf), "%.1f", aht.temperatureC);
        dirty |= drawField(c.bigtemp, sizeof(c.bigtemp), buf, 20, 70, 8, F24, COLOR_NORMAL);
        dirty |= drawField(c.bigunit, sizeof(c.bigunit), "C",   120, 70, 2, F24, COLOR_DIM);
        snprintf(buf, sizeof(buf), "Humid %.1f %%", aht.humidityPct);
        dirty |= drawField(c.humid, sizeof(c.humid), buf, 5, 120, 18, F24, COLOR_ACCENT);
    } else {
        dirty |= drawField(c.bigtemp, sizeof(c.bigtemp), "--", 20, 70, 8, F24, COLOR_DIM);
    }

    snprintf(buf, sizeof(buf), "Light %d lx", (int)lux.lux);
    dirty |= drawField(c.light, sizeof(c.light), buf, 5, 160, 18, F24, COLOR_ACCENT);

    if (aht.valid) {
        const char* comfort = "Normal";
        uint32_t c_c = COLOR_WARN;
        if (aht.temperatureC > 18 && aht.temperatureC < 28 &&
            aht.humidityPct > 30 && aht.humidityPct < 70) {
            comfort = "Comfort";
            c_c = COLOR_NORMAL;
        }
        snprintf(buf, sizeof(buf), "Feel: %s", comfort);
        dirty |= drawField(c.comfort, sizeof(c.comfort), buf, 5, 200, 18, F24, c_c);
    } else {
        dirty |= drawField(c.comfort, sizeof(c.comfort), "Feel: --", 5, 200, 18, F24, COLOR_DIM);
    }

    dirty |= drawField(c.hint, sizeof(c.hint), "[A] Next", 5, 295, 22, F16, COLOR_DIM);
    return dirty;
}

static bool render_portrait_settings() {
    PageCache& c = g_cache[PAGE_PORTRAIT_SETTINGS];
    bool dirty = false;

    drawOnce(&c.ever_drawn, 5, 5, "Settings", COLOR_TITLE, F24);
    drawOnceLine(&c.ever_drawn, 5, 55, 235, COLOR_DIM);
    dirty |= draw_status_bar(PAGE_PORTRAIT_SETTINGS);

    // 全是静态文本；用 ever_drawn 守门
    static const char* lines[] = {
        "Power   Balanced",  "  30s / 90s",
        "Sync    Battery",   "  22min / 60min",
        "Density Normal",
        "Rotate  Auto",
        "Theme   Dark",
    };
    if (!c.ever_drawn) {
        int y = 65;
        for (uint8_t i = 0; i < 5; ++i) {
            k10.canvas->canvasText(lines[i], 5, y, COLOR_NORMAL, F24, 50, false);
            y += 28;
        }
        k10.canvas->canvasText("[A+B] Factory reset", 5, 295, COLOR_ALERT, F16, 50, false);
        c.ever_drawn = true;
        dirty = true;
    }
    return dirty;
}

// ---------------------------------------------------------------------------
// 横屏页面
// ---------------------------------------------------------------------------
static bool render_landscape_overview() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_OVERVIEW];
    bool dirty = false;
    drawOnce(&c.ever_drawn, 5, 5, "DeskNest", COLOR_TITLE, F24);
    dirty |= draw_status_bar(PAGE_LANDSCAPE_OVERVIEW);
    const auto aht = g_sensors.aht20();
    char buf[40];
    if (aht.valid) {
        snprintf(buf, sizeof(buf), "%.1fC %.1f%%", aht.temperatureC, aht.humidityPct);
        dirty |= drawField(c.temp, sizeof(c.temp), buf, 5, 60, 16, F24, COLOR_NORMAL);
    } else {
        dirty |= drawField(c.temp, sizeof(c.temp), "--", 5, 60, 16, F24, COLOR_DIM);
    }
    dirty |= drawField(c.hint, sizeof(c.hint), "[A] Next", 5, 295, 22, F16, COLOR_DIM);
    return dirty;
}

static bool render_landscape_focus() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_FOCUS];
    bool dirty = false;
    drawOnce(&c.ever_drawn, 5, 5, "AI Focus", COLOR_TITLE, F24);
    dirty |= draw_status_bar(PAGE_LANDSCAPE_FOCUS);
    AiUsage u = mock_ai_usage(millis());
    char buf[24];
    snprintf(buf, sizeof(buf), "ChatGPT %d%%", u.chatgpt_pct);
    dirty |= drawField(c.subline, sizeof(c.subline), buf, 5, 60, 16, F24, COLOR_NORMAL);
    dirty |= drawBar(&c.chatgpt_bar_w, u.chatgpt_pct, 5, 90, 200, 16,
                     bar_color(u.chatgpt_pct));
    snprintf(buf, sizeof(buf), "Claude %d%%", u.claude_pct);
    dirty |= drawField(c.hint, sizeof(c.hint), buf, 5, 120, 16, F24, COLOR_NORMAL);
    dirty |= drawBar(&c.claude_bar_w, u.claude_pct, 5, 150, 200, 16,
                     bar_color(u.claude_pct));
    return dirty;
}

static bool render_landscape_custom() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_CUSTOM];
    bool dirty = false;
    drawOnce(&c.ever_drawn, 5, 5, "Custom", COLOR_TITLE, F24);
    dirty |= draw_status_bar(PAGE_LANDSCAPE_CUSTOM);
    dirty |= drawField(c.subline, sizeof(c.subline), "(P1 user config)",
                       5, 120, 22, F24, COLOR_DIM);
    dirty |= drawField(c.hint, sizeof(c.hint), "[A] Next", 5, 295, 22, F16, COLOR_DIM);
    return dirty;
}

// ---------------------------------------------------------------------------
// 特殊页
// ---------------------------------------------------------------------------
static bool render_face_down() {
    PageCache& c = g_cache[PAGE_SLEEP_FACE_DOWN];
    bool dirty = false;
    if (!c.ever_drawn) {
        k10.canvas->canvasClear();  // 翻面后进 roost，整个 canvas 黑底
        c.ever_drawn = true;
        dirty = true;
    }
    dirty |= drawField(c.slogan1, sizeof(c.slogan1), "Perched on desk,",  40, 120, 18, F24, COLOR_DIM);
    dirty |= drawField(c.slogan2, sizeof(c.slogan2), "dormant between",   20, 150, 18, F24, COLOR_DIM);
    dirty |= drawField(c.slogan3, sizeof(c.slogan3), "wake-ups.",         70, 180, 12, F24, COLOR_DIM);
    return dirty;
}

static bool render_config_portal() {
    PageCache& c = g_cache[PAGE_CONFIG_PORTAL];
    bool dirty = false;
    drawOnce(&c.ever_drawn, 5, 5, "WiFi Setup", COLOR_TITLE, F24);
    drawOnceLine(&c.ever_drawn, 5, 55, 235, COLOR_DIM);
    dirty |= draw_status_bar(PAGE_CONFIG_PORTAL);
    dirty |= drawField(c.subline, sizeof(c.subline), "Connect to:", 5, 80, 16, F24, COLOR_TITLE);
    dirty |= drawField(c.ssid_line, sizeof(c.ssid_line), "DeskNest-XXXX", 5, 110, 16, F24, COLOR_ACCENT);
    dirty |= drawField(c.hint, sizeof(c.hint), "Open browser:", 5, 160, 16, F24, COLOR_TITLE);
    dirty |= drawField(c.url_line, sizeof(c.url_line), "192.168.4.1", 5, 190, 14, F24, COLOR_ACCENT);
    return dirty;
}

// ---------------------------------------------------------------------------
// 调度
// ---------------------------------------------------------------------------
static bool render_dispatch(UIPage p) {
    switch (p) {
        case PAGE_PORTRAIT_OVERVIEW:    return render_portrait_overview();
        case PAGE_PORTRAIT_AI_USAGE:    return render_portrait_ai_usage();
        case PAGE_PORTRAIT_ENVIRONMENT: return render_portrait_environment();
        case PAGE_PORTRAIT_SETTINGS:    return render_portrait_settings();
        case PAGE_LANDSCAPE_OVERVIEW:   return render_landscape_overview();
        case PAGE_LANDSCAPE_FOCUS:      return render_landscape_focus();
        case PAGE_LANDSCAPE_CUSTOM:     return render_landscape_custom();
        case PAGE_SLEEP_FACE_DOWN:      return render_face_down();
        case PAGE_CONFIG_PORTAL:        return render_config_portal();
        default:                        return false;
    }
}

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
    Serial.println("[D][UI] screen ready (dir=2, canvas 240x320, dirty-update)");

    k10.rgb->write(-1, RGB_OFF);
    for (uint8_t i = 0; i < PAGE_COUNT; ++i) g_cache[i] = PageCache();
    g_last_page = PAGE_COUNT;
}

void dn_ui_render() {
    using namespace desknest;
    const UIPage cur = g_state.snapshot().page;

    // 翻页：清屏 + 整页重画（cache 已重置，render_dispatch 会全画）
    if (cur != g_last_page) {
        k10.canvas->canvasClear();
        g_cache[cur] = PageCache();      // 强制全画
        g_last_page = cur;
    }

    // 字段级 diff 更新；没变就不动
    if (render_dispatch(cur)) {
        k10.canvas->updateCanvas();      // 只有真改了才推 SPI
    }
    rgb_off();
}