// src/ui.cpp
// 栖屏 DeskNest - UI 渲染层（现代化重设计版）
// "栖于桌面，息于常亮之间"
//
// 设计原则（K10 240×320 受限硬件下最大化现代感）：
//   1. 强排版层级 —— hero 数据大、secondary 小、status 暗
//   2. 少边框多留白 —— 用 1px 细线 divider 替代 box 边框
//   3. 配色有意义 —— 绿=ok / 橙=warn / 红=alert / 青=交互 / 白=主数据
//   4. 统一 header / footer —— 每页都有标题 + P{n}/{N} 页码 + 按钮提示 + idle
//   5. 状态点 —— canvasCircle 画小圆点做状态指示（舒适度、电池等）
//   6. 脏区局部刷新 —— canvasClear 只在翻页时调；同页字段级 diff

#include "ui.h"
#include "config.h"
#include "sensors.h"
#include "gesture.h"
#include "state_machine.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <unihiker_k10.h>

namespace desknest {

extern UNIHIKER_K10 k10;

namespace {

// ---------------------------------------------------------------------------
// 颜色系统（扩展：增加卡片背景 / 分隔线）
// ---------------------------------------------------------------------------
constexpr uint32_t COLOR_BG       = 0x000000;   // 屏幕底色
constexpr uint32_t COLOR_CARD     = 0x111722;   // 卡片底色（比 BG 略亮）
constexpr uint32_t COLOR_LINE     = 0x263244;   // 分隔线
constexpr uint32_t COLOR_TITLE    = 0xFFFFFF;   // 主数据 / 标题
constexpr uint32_t COLOR_LABEL    = 0x7F91A8;   // 标签（次级文字）
constexpr uint32_t COLOR_DIM      = 0x808080;   // 提示 / 旧保留
constexpr uint32_t COLOR_NORMAL   = 0x69DB9C;   // 绿（好 / 正常）
constexpr uint32_t COLOR_WARN     = 0xFFBD66;   // 橙（警告）
constexpr uint32_t COLOR_ALERT    = 0xFF7272;   // 红（严重）
constexpr uint32_t COLOR_ACCENT   = 0x42D3FF;   // 青（交互 / 高亮）

constexpr uint32_t RGB_OFF        = 0x000000;

constexpr auto F24 = Canvas::eCNAndENFont24;
constexpr auto F16 = Canvas::eCNAndENFont16;
constexpr auto F12 = Canvas::eGreeceFont12x24;   // 12px 窄体，紧凑排版用

// ---------------------------------------------------------------------------
// 布局常量
// ---------------------------------------------------------------------------
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 320;

// 内容区域：header(24) + divider(2) + content(228) + divider(2) + footer(20)
//   + shake bar(10) + bottom safe(34)
constexpr int HEADER_Y    = 5;
constexpr int DIVIDER_Y1  = 27;
constexpr int CONTENT_Y0  = 36;
constexpr int DIVIDER_Y2  = 280;
constexpr int FOOTER_Y    = 286;
constexpr int SHAKE_BAR_Y = 305;
constexpr int SHAKE_BAR_H = 8;

// 字宽估算（canvas 没提供 measure 接口，估准就行）
static int charW(Canvas::eFontSize_t f) {
    if (f == F24) return 14;
    if (f == F16) return 9;
    return 8;   // F12
}
static int lineH(Canvas::eFontSize_t f) {
    if (f == F24) return 28;
    if (f == F16) return 18;
    return 24;  // F12
}

// ---------------------------------------------------------------------------
// PageCache：每页一份；drawField 比对 + 写入
// ---------------------------------------------------------------------------
struct PageCache {
    bool    ever_drawn  = false;

    // header
    char    title[20]   = "";

    // status row (header 下方第二行)
    char    sysstate[24] = "";
    char    pagenum[8]  = "";

    // footer
    char    hint[40]    = "";
    char    idle[24]    = "";

    // Overview
    char    hero_label[8]   = "";     // "Temp"
    char    hero_value[16]  = "";     // "22.5°C"
    char    metric_row[28]  = "";     // "Humid 58% · 240 lx"
    char    status_row[32]  = "";     // "● Portrait 87%"

    // AI Usage
    char    ai_title_a[20]  = "";     // "ChatGPT Plus"
    char    ai_title_b[20]  = "";     // "Claude Pro"
    char    ai_pct_a[8]     = "";     // "82%"
    char    ai_pct_b[8]     = "";     // "45%"
    int     ai_bar_a_w      = -1;
    int     ai_bar_b_w      = -1;
    char    ai_hint[24]     = "";

    // Environment
    char    env_comfort[16] = "";
    char    env_secondary[40] = "";

    // Settings
    char    settings_lines[200] = "";  // 整块（一行一字段名+值+箭头）

    // Landscape Overview
    char    land_hero_l_label[8]  = "";
    char    land_hero_l_value[16] = "";
    char    land_hero_r_label[8]  = "";
    char    land_hero_r_value[16] = "";
    char    land_sub_l_label[8]   = "";
    char    land_sub_l_value[16]  = "";
    char    land_sub_r_label[8]   = "";
    char    land_sub_r_value[16]  = "";

    // Face-down 三行
    char    fd_dot[2]        = "";   // "·" 或空
    char    fd_l1[24]        = "";
    char    fd_l2[24]        = "";
    char    fd_l3[24]        = "";

    // Config
    char    cfg_label[16]    = "";    // "Connect to"
    char    cfg_ssid[24]     = "";    // "DeskNest-XXXX"
    char    cfg_label2[16]   = "";    // "Open browser"
    char    cfg_url[24]      = "";    // "192.168.4.1"
};

static PageCache g_cache[PAGE_COUNT];
static UIPage   g_last_page = PAGE_COUNT;
static ShakePhase g_last_shake_phase = SHAKE_PHASE_IDLE;

// ---------------------------------------------------------------------------
// 底层绘制
// ---------------------------------------------------------------------------

// drawField：值变了才画 —— 先黑矩形擦老的一块，再写新值
static bool drawField(char* cache, size_t cap, const char* new_val,
                      int x, int y, int max_chars,
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
    k10.canvas->canvasRectangle(x, y, w, h, COLOR_CARD, COLOR_CARD, true);
    if (new_w > 0) k10.canvas->canvasRectangle(x, y, new_w, h, bar_c, bar_c, true);
    *cache_w = new_w;
    return true;
}

// 一次性绘制（ever_drawn 守门）
static void drawOnce(bool* flag, int x, int y, const char* s,
                     uint32_t color, Canvas::eFontSize_t font) {
    if (*flag) return;
    k10.canvas->canvasText(s, x, y, color, font, 50, false);
    *flag = true;
}
static void drawOnceLine(bool* flag, int x1, int y, int x2, uint32_t color) {
    if (*flag) return;
    k10.canvas->canvasLine(x1, y, x2, y, color);
    *flag = true;
}

// ---------------------------------------------------------------------------
// 通用：status 文字 + 页码 header
// ---------------------------------------------------------------------------
static const char* sys_str(SystemState s) {
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

static bool draw_header(UIPage p, const char* title) {
    PageCache& c = g_cache[p];
    bool dirty = false;
    dirty |= drawField(c.title, sizeof(c.title), title,
                       5, HEADER_Y, 16, F24, COLOR_TITLE);
    char pid[8];
    snprintf(pid, sizeof(pid), "P%u/%u", (unsigned)(p + 1), (unsigned)PAGE_COUNT);
    dirty |= drawField(c.pagenum, sizeof(c.pagenum), pid,
                       195, HEADER_Y, 6, F24, COLOR_ACCENT);
    return dirty;
}

static bool draw_status_strip(UIPage p) {
    PageCache& c = g_cache[p];
    const auto& s = g_state.snapshot();
    char buf[32];
    snprintf(buf, sizeof(buf), "● %s", sys_str(s.system));
    bool dirty = drawField(c.sysstate, sizeof(c.sysstate), buf,
                           5, 38, 18, F16, COLOR_LABEL);
    char ib[16];
    snprintf(ib, sizeof(ib), "idle %lus", (unsigned long)((millis() - s.lastInputMs) / 1000));
    dirty |= drawField(c.idle, sizeof(c.idle), ib,
                       170, 38, 14, F16, COLOR_LABEL);
    return dirty;
}

static bool draw_footer(UIPage p, const char* hint) {
    PageCache& c = g_cache[p];
    bool dirty = false;
    // divider 在 footer 上方（每页画一次）
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y2, 235, COLOR_LINE);
    // button hint（居左）
    dirty |= drawField(c.hint, sizeof(c.hint), hint,
                       5, FOOTER_Y, 28, F16, COLOR_LABEL);
    return dirty;
}

// ---------------------------------------------------------------------------
// 数据辅助
// ---------------------------------------------------------------------------
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
uint32_t battery_color(int pct) {
    if (pct > 50) return COLOR_NORMAL;
    if (pct > 20) return COLOR_WARN;
    return COLOR_ALERT;
}

// 三段式甩动反馈：OUTBOUND/RETURN 进度条
static bool draw_shake_transition() {
    const ShakePhase phase = g_gesture.shakePhase();
    if (phase == g_last_shake_phase) return false;

    constexpr int y = SHAKE_BAR_Y;
    constexpr int h = SHAKE_BAR_H;
    constexpr int w = SCREEN_W;
    // 擦干净
    k10.canvas->canvasRectangle(0, y, w, h, COLOR_BG, COLOR_BG, true);
    int pct = shakeAnimationPercent(phase);
    if (pct > 0) {
        const int pw = (w * pct) / 100;
        const int x = (g_gesture.shakeDirection() > 0) ? 0 : (w - pw);
        uint32_t c = (phase == SHAKE_PHASE_OUTBOUND) ? COLOR_WARN : COLOR_ACCENT;
        if (pw > 0) k10.canvas->canvasRectangle(x, y, pw, h, c, c, true);
    }
    g_last_shake_phase = phase;
    return true;
}

// ===========================================================================
// Portrait 页面
// ===========================================================================

// P1: Overview — Hero 温度 + 次级数据 + 状态条
static bool render_portrait_overview() {
    PageCache& c = g_cache[PAGE_PORTRAIT_OVERVIEW];
    bool dirty = draw_header(PAGE_PORTRAIT_OVERVIEW, "DeskNest");
    dirty |= draw_status_strip(PAGE_PORTRAIT_OVERVIEW);

    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    // HERO: 温度
    const auto aht = g_sensors.aht20();
    const auto lux = g_sensors.ltr303();
    const auto bat = g_sensors.battery();

    dirty |= drawField(c.hero_label, sizeof(c.hero_label),
                       "Temp", 5, 75, 6, F16, COLOR_LABEL);

    char hv[16];
    if (aht.valid) snprintf(hv, sizeof(hv), "%.1f°C", aht.temperatureC);
    else          snprintf(hv, sizeof(hv), "--°C");
    dirty |= drawField(c.hero_value, sizeof(c.hero_value), hv,
                       5, 95, 12, F24, COLOR_TITLE);

    // 次级：湿度 + 光照（inline）
    char mr[40];
    if (aht.valid) {
        snprintf(mr, sizeof(mr), "Humid %.0f%%  ·  %d lx",
                 aht.humidityPct, (int)lux.lux);
    } else {
        snprintf(mr, sizeof(mr), "Humid --  ·  %d lx", (int)lux.lux);
    }
    dirty |= drawField(c.metric_row, sizeof(c.metric_row), mr,
                       5, 140, 26, F16, COLOR_LABEL);

    // 状态条：姿态 + 电量
    char sr[40];
    const auto& s = g_state.snapshot();
    const char* orient = (s.orientation == ORIENTATION_PORTRAIT) ? "Portrait"
                       : (s.orientation == ORIENTATION_LANDSCAPE) ? "Landscape"
                       : (s.orientation == ORIENTATION_FACE_DOWN) ? "Face-Dn"
                       : "Unknown";
    if (bat.valid) {
        snprintf(sr, sizeof(sr), "● %s  ·  %d%%", orient, bat.percent);
    } else {
        snprintf(sr, sizeof(sr), "● %s  ·  USB", orient);
    }
    dirty |= drawField(c.status_row, sizeof(c.status_row), sr,
                       5, 175, 24, F24, COLOR_NORMAL);

    dirty |= draw_footer(PAGE_PORTRAIT_OVERVIEW, "[A] Next  [B] Prev");
    dirty |= draw_shake_transition();
    return dirty;
}

// P2: AI Usage — 两条大进度条 + 百分比
static bool render_portrait_ai_usage() {
    PageCache& c = g_cache[PAGE_PORTRAIT_AI_USAGE];
    bool dirty = draw_header(PAGE_PORTRAIT_AI_USAGE, "AI Usage");
    dirty |= draw_status_strip(PAGE_PORTRAIT_AI_USAGE);
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    AiUsage u = mock_ai_usage(millis());

    // ChatGPT
    dirty |= drawField(c.ai_title_a, sizeof(c.ai_title_a),
                       "ChatGPT Plus", 5, 75, 16, F24, COLOR_TITLE);
    char pa[8]; snprintf(pa, sizeof(pa), "%d%%", u.chatgpt_pct);
    dirty |= drawField(c.ai_pct_a, sizeof(c.ai_pct_a), pa,
                       195, 80, 6, F16, bar_color(u.chatgpt_pct));
    dirty |= drawBar(&c.ai_bar_a_w, u.chatgpt_pct, 5, 110, 230, 14,
                     bar_color(u.chatgpt_pct));

    // Claude
    dirty |= drawField(c.ai_title_b, sizeof(c.ai_title_b),
                       "Claude Pro", 5, 150, 16, F24, COLOR_TITLE);
    char pb[8]; snprintf(pb, sizeof(pb), "%d%%", u.claude_pct);
    dirty |= drawField(c.ai_pct_b, sizeof(c.ai_pct_b), pb,
                       195, 155, 6, F16, bar_color(u.claude_pct));
    dirty |= drawBar(&c.ai_bar_b_w, u.claude_pct, 5, 185, 230, 14,
                     bar_color(u.claude_pct));

    dirty |= drawField(c.ai_hint, sizeof(c.ai_hint),
                       "P1: cc-switch API", 5, 225, 22, F16, COLOR_LABEL);

    dirty |= draw_footer(PAGE_PORTRAIT_AI_USAGE, "[A] Next");
    dirty |= draw_shake_transition();
    return dirty;
}

// P3: Environment — Hero 温度 + 舒适度状态点
static bool render_portrait_environment() {
    PageCache& c = g_cache[PAGE_PORTRAIT_ENVIRONMENT];
    bool dirty = draw_header(PAGE_PORTRAIT_ENVIRONMENT, "Environment");
    dirty |= draw_status_strip(PAGE_PORTRAIT_ENVIRONMENT);
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    const auto aht = g_sensors.aht20();
    const auto lux = g_sensors.ltr303();

    dirty |= drawField(c.hero_label, sizeof(c.hero_label),
                       "Temp", 5, 75, 6, F16, COLOR_LABEL);
    char hv[16];
    if (aht.valid) snprintf(hv, sizeof(hv), "%.1f°C", aht.temperatureC);
    else          snprintf(hv, sizeof(hv), "--°C");
    dirty |= drawField(c.hero_value, sizeof(c.hero_value), hv,
                       5, 95, 12, F24, COLOR_TITLE);

    // 舒适度（带状态点）
    if (aht.valid) {
        const bool comfort = (aht.temperatureC > 18 && aht.temperatureC < 28 &&
                              aht.humidityPct > 30 && aht.humidityPct < 70);
        char cs[24];
        snprintf(cs, sizeof(cs), "●  %s",
                 comfort ? "Comfort" : (aht.temperatureC < 18 ? "Cold" : "Warm"));
        dirty |= drawField(c.env_comfort, sizeof(c.env_comfort), cs,
                           5, 145, 14, F24, comfort ? COLOR_NORMAL : COLOR_WARN);
    } else {
        dirty |= drawField(c.env_comfort, sizeof(c.env_comfort),
                           "●  --", 5, 145, 6, F24, COLOR_LABEL);
    }

    // 二级：湿度 + 光照
    char sec[40];
    if (aht.valid) {
        snprintf(sec, sizeof(sec), "Humid %.0f%%  ·  Light %d lx",
                 aht.humidityPct, (int)lux.lux);
    } else {
        snprintf(sec, sizeof(sec), "Humid --  ·  Light %d lx", (int)lux.lux);
    }
    dirty |= drawField(c.env_secondary, sizeof(c.env_secondary), sec,
                       5, 195, 26, F16, COLOR_LABEL);

    dirty |= draw_footer(PAGE_PORTRAIT_ENVIRONMENT, "[A] Next");
    dirty |= draw_shake_transition();
    return dirty;
}

// P4: Settings — 列表视图 + 箭头 + 红色 factory reset
static bool render_portrait_settings() {
    PageCache& c = g_cache[PAGE_PORTRAIT_SETTINGS];
    bool dirty = draw_header(PAGE_PORTRAIT_SETTINGS, "Settings");
    dirty |= draw_status_strip(PAGE_PORTRAIT_SETTINGS);
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    // 整块字符串缓存（任一字段变化整块重画）
    char block[200];
    snprintf(block, sizeof(block),
        "Power      Balanced   ▸\n"
        "Sync       Battery    ▸\n"
        "Density    Normal     ▸\n"
        "Rotate     Auto       ▸\n"
        "Theme      Dark       ▸");

    if (strcmp(c.settings_lines, block) != 0) {
        // 整块擦
        k10.canvas->canvasRectangle(5, 70, 230, 170, COLOR_BG, COLOR_BG, true);
        int y = 70;
        const char* line = block;
        while (line && *line) {
            const char* nl = strchr(line, '\n');
            char one[40];
            if (nl) {
                size_t n = nl - line; if (n >= sizeof(one)) n = sizeof(one) - 1;
                memcpy(one, line, n); one[n] = '\0';
                line = nl + 1;
            } else {
                strncpy(one, line, sizeof(one) - 1); one[sizeof(one) - 1] = '\0';
                line = nullptr;
            }
            // 找到 ▸ 分隔：前半 label（TITLE），后半 value（ACCENT）
            const char* arrow = strstr(one, " ▸");
            if (arrow) {
                size_t ll = arrow - one;
                char left[24], right[16];
                if (ll >= sizeof(left)) ll = sizeof(left) - 1;
                memcpy(left, one, ll); left[ll] = '\0';
                strncpy(right, arrow + 3, sizeof(right) - 1); right[sizeof(right) - 1] = '\0';
                k10.canvas->canvasText(left, 5, y, COLOR_TITLE, F24, 50, false);
                k10.canvas->canvasText(right, 5 + ll * charW(F24), y, COLOR_ACCENT, F24, 50, false);
            } else {
                k10.canvas->canvasText(one, 5, y, COLOR_TITLE, F24, 50, false);
            }
            y += 28;
        }
        strncpy(c.settings_lines, block, sizeof(c.settings_lines) - 1);
        c.settings_lines[sizeof(c.settings_lines) - 1] = '\0';
        dirty = true;
    }

    // 红色 factory reset
    if (!c.ever_drawn) {
        k10.canvas->canvasText("[A+B] Factory reset",
                               5, 245, COLOR_ALERT, F16, 50, false);
        c.ever_drawn = true;
        dirty = true;
    }

    dirty |= draw_footer(PAGE_PORTRAIT_SETTINGS, "");
    dirty |= draw_shake_transition();
    return dirty;
}

// ===========================================================================
// Landscape 页面（480 逻辑宽 —— 实际物理仍是 240 旋转后的视图）
// 注：K10 BSP 用同一 240x320 canvas，landscape 通过旋转显示方向实现
// 这里按 480×320 排版，画到 (0..239) 旋转后呈现成横屏
// ===========================================================================

// P5: Landscape Overview — 两列 hero + 两列 secondary
static bool render_landscape_overview() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_OVERVIEW];
    // 横屏：title 缩短，页码在右上
    bool dirty = draw_header(PAGE_LANDSCAPE_OVERVIEW, "DeskNest");
    dirty |= draw_status_strip(PAGE_LANDSCAPE_OVERVIEW);
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    const auto aht = g_sensors.aht20();
    const auto lux = g_sensors.ltr303();
    const auto bat = g_sensors.battery();
    const auto& s = g_state.snapshot();

    // 左列 hero：温度；右列 hero：湿度
    dirty |= drawField(c.land_hero_l_label, sizeof(c.land_hero_l_label),
                       "TEMP", 5, 75, 4, F16, COLOR_LABEL);
    char lv[16]; snprintf(lv, sizeof(lv), "%.1f°C", aht.valid ? aht.temperatureC : 0);
    if (!aht.valid) snprintf(lv, sizeof(lv), "--°C");
    dirty |= drawField(c.land_hero_l_value, sizeof(c.land_hero_l_value), lv,
                       5, 92, 12, F24, COLOR_TITLE);

    dirty |= drawField(c.land_hero_r_label, sizeof(c.land_hero_r_label),
                       "HUMID", 125, 75, 5, F16, COLOR_LABEL);
    char rv[16]; snprintf(rv, sizeof(rv), "%.0f%%", aht.valid ? aht.humidityPct : 0);
    if (!aht.valid) snprintf(rv, sizeof(rv), "--%%");
    dirty |= drawField(c.land_hero_r_value, sizeof(c.land_hero_r_value), rv,
                       125, 92, 12, F24, COLOR_TITLE);

    // 左列 secondary：光照；右列 secondary：电量
    dirty |= drawField(c.land_sub_l_label, sizeof(c.land_sub_l_label),
                       "LIGHT", 5, 145, 5, F16, COLOR_LABEL);
    char ll[16]; snprintf(ll, sizeof(ll), "%d lx", (int)lux.lux);
    dirty |= drawField(c.land_sub_l_value, sizeof(c.land_sub_l_value), ll,
                       5, 162, 12, F24, COLOR_ACCENT);

    dirty |= drawField(c.land_sub_r_label, sizeof(c.land_sub_r_label),
                       "BATT", 125, 145, 4, F16, COLOR_LABEL);
    char lr[16];
    if (bat.valid) snprintf(lr, sizeof(lr), "%d%%", bat.percent);
    else           snprintf(lr, sizeof(lr), "USB");
    dirty |= drawField(c.land_sub_r_value, sizeof(c.land_sub_r_value), lr,
                       125, 162, 12, F24,
                       bat.valid ? battery_color(bat.percent) : COLOR_LABEL);

    // 状态行
    char sr[32];
    const char* orient = (s.orientation == ORIENTATION_PORTRAIT) ? "Portrait"
                       : (s.orientation == ORIENTATION_LANDSCAPE) ? "Landscape"
                       : (s.orientation == ORIENTATION_FACE_DOWN) ? "Face-Dn"
                       : "Unknown";
    snprintf(sr, sizeof(sr), "● %s", orient);
    dirty |= drawField(c.status_row, sizeof(c.status_row), sr,
                       5, 215, 16, F24, COLOR_NORMAL);

    dirty |= draw_footer(PAGE_LANDSCAPE_OVERVIEW, "[A] Next  [B] Prev");
    dirty |= draw_shake_transition();
    return dirty;
}

// P6: Landscape Focus — 未来 P1 专注计时占位
static bool render_landscape_focus() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_FOCUS];
    bool dirty = draw_header(PAGE_LANDSCAPE_FOCUS, "Focus");
    dirty |= draw_status_strip(PAGE_LANDSCAPE_FOCUS);
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    // AI 用量（横向条）
    AiUsage u = mock_ai_usage(millis());

    dirty |= drawField(c.ai_title_a, sizeof(c.ai_title_a),
                       "ChatGPT", 5, 75, 8, F24, COLOR_TITLE);
    char pa[8]; snprintf(pa, sizeof(pa), "%d%%", u.chatgpt_pct);
    dirty |= drawField(c.ai_pct_a, sizeof(c.ai_pct_a), pa,
                       195, 80, 6, F16, bar_color(u.chatgpt_pct));
    dirty |= drawBar(&c.ai_bar_a_w, u.chatgpt_pct, 5, 110, 230, 14,
                     bar_color(u.chatgpt_pct));

    dirty |= drawField(c.ai_title_b, sizeof(c.ai_title_b),
                       "Claude", 5, 145, 8, F24, COLOR_TITLE);
    char pb[8]; snprintf(pb, sizeof(pb), "%d%%", u.claude_pct);
    dirty |= drawField(c.ai_pct_b, sizeof(c.ai_pct_b), pb,
                       195, 150, 6, F16, bar_color(u.claude_pct));
    dirty |= drawBar(&c.ai_bar_b_w, u.claude_pct, 5, 180, 230, 14,
                     bar_color(u.claude_pct));

    dirty |= drawField(c.ai_hint, sizeof(c.ai_hint),
                       "P1: focus timer", 5, 220, 18, F16, COLOR_LABEL);

    dirty |= draw_footer(PAGE_LANDSCAPE_FOCUS, "[A] Next  [B] Prev");
    dirty |= draw_shake_transition();
    return dirty;
}

// P7: Landscape Custom — 占位
static bool render_landscape_custom() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_CUSTOM];
    bool dirty = draw_header(PAGE_LANDSCAPE_CUSTOM, "Custom");
    dirty |= draw_status_strip(PAGE_LANDSCAPE_CUSTOM);
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    // 居中 placeholder
    if (!c.ever_drawn) {
        // 占位灰底卡
        k10.canvas->canvasRectangle(40, 110, 160, 80, COLOR_CARD, COLOR_CARD, true);
        k10.canvas->canvasText("(P1 user config)",
                               65, 145, COLOR_LABEL, F24, 50, false);
        c.ever_drawn = true;
        dirty = true;
    }
    dirty |= draw_footer(PAGE_LANDSCAPE_CUSTOM, "[A] Next  [B] Prev");
    dirty |= draw_shake_transition();
    return dirty;
}

// ===========================================================================
// 特殊页
// ===========================================================================

// SLP: Face-Down — 极简中央 slogan
static bool render_face_down() {
    PageCache& c = g_cache[PAGE_SLEEP_FACE_DOWN];
    bool dirty = false;

    // Face-down 整页黑（roost 模式，屏灭感）
    if (!c.ever_drawn) {
        k10.canvas->canvasClear();
        c.ever_drawn = true;
        dirty = true;
    }

    // 中央小圆点（呼吸感；这里只静态显示）
    dirty |= drawField(c.fd_dot, sizeof(c.fd_dot), "·",
                       116, 100, 1, F24, COLOR_LABEL);

    dirty |= drawField(c.fd_l1, sizeof(c.fd_l1),
                       "Perched on desk,", 40, 145, 18, F24, COLOR_LABEL);
    dirty |= drawField(c.fd_l2, sizeof(c.fd_l2),
                       "dormant between", 30, 175, 18, F24, COLOR_LABEL);
    dirty |= drawField(c.fd_l3, sizeof(c.fd_l3),
                       "wake-ups.", 75, 205, 12, F24, COLOR_LABEL);

    return dirty;
}

// CFG: WiFi 配网页
static bool render_config_portal() {
    PageCache& c = g_cache[PAGE_CONFIG_PORTAL];
    bool dirty = draw_header(PAGE_CONFIG_PORTAL, "WiFi Setup");
    dirty |= draw_status_strip(PAGE_CONFIG_PORTAL);
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    dirty |= drawField(c.cfg_label, sizeof(c.cfg_label),
                       "Connect to", 5, 80, 12, F24, COLOR_TITLE);
    dirty |= drawField(c.cfg_ssid, sizeof(c.cfg_ssid),
                       "DeskNest-XXXX", 5, 115, 16, F24, COLOR_ACCENT);

    dirty |= drawField(c.cfg_label2, sizeof(c.cfg_label2),
                       "Open browser", 5, 170, 14, F24, COLOR_TITLE);
    dirty |= drawField(c.cfg_url, sizeof(c.cfg_url),
                       "192.168.4.1", 5, 205, 14, F24, COLOR_ACCENT);

    dirty |= draw_footer(PAGE_CONFIG_PORTAL, "");
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
    Serial.println("[D][UI] screen ready (240x320, modern dark theme)");

    k10.rgb->write(-1, RGB_OFF);
    for (uint8_t i = 0; i < PAGE_COUNT; ++i) g_cache[i] = PageCache();
    g_last_page = PAGE_COUNT;
    g_last_shake_phase = SHAKE_PHASE_IDLE;
}

void dn_ui_render() {
    using namespace desknest;
    const UIPage cur = g_state.snapshot().page;

    if (cur != g_last_page) {
        k10.canvas->canvasClear();
        g_cache[cur] = PageCache();
        g_last_page = cur;
    }

    if (render_dispatch(cur)) {
        k10.canvas->updateCanvas();
    }
    rgb_off();
}