// src/ui.cpp
// 栖屏 DeskNest - UI 渲染层（Morandi 主题 + 减密重设计）
// "栖于桌面，息于常亮之间"
//
// 设计原则：
//   1. **莫兰迪绿为主色**  —— 莫兰迪绿 #9CB89B 用作页码、状态点、按钮高亮
//      整套配色走低饱和：暖黑底 + 莫兰迪 sage + 暖白字
//   2. **减密 + 大留白**   —— 240×320 实际挤，每页只放一个 F24 HERO，其余 F16
//   3. **少边框多留白**   —— 用细线 divider 替代 box 边框
//   4. **统一 header/footer** —— 每页都有标题 + P{n}/N + 按钮提示 + idle
//   5. **脏区局部刷新**   —— canvasClear 只在翻页时；同页字段级 diff

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
// Morandi 调色板（莫兰迪风格的低饱和柔和色系）
// ---------------------------------------------------------------------------
constexpr uint32_t COLOR_BG       = 0x0A0E14;   // 暖黑底（带极轻蓝调）
constexpr uint32_t COLOR_CARD     = 0x161D27;   // 卡片底（略亮）
constexpr uint32_t COLOR_LINE     = 0x2A3447;   // 分隔线
constexpr uint32_t COLOR_TITLE    = 0xE8E4D8;   // 暖白主数据
constexpr uint32_t COLOR_LABEL    = 0x8B95A8;   // 次级灰
constexpr uint32_t COLOR_DIM      = 0x808080;   // 提示
constexpr uint32_t COLOR_NORMAL   = 0x69DB9C;   // 亮绿（"好"强调）
constexpr uint32_t COLOR_WARN     = 0xE8A87C;   // 莫兰迪橙
constexpr uint32_t COLOR_ALERT    = 0xC97070;   // 莫兰迪红
constexpr uint32_t COLOR_MORANDI  = 0x9CB89B;   // ★ 莫兰迪绿（主色）
constexpr uint32_t COLOR_MORANDI_D = 0x7A9A7B;   // 莫兰迪绿深色（描边/分隔用）

constexpr uint32_t RGB_OFF        = 0x000000;

constexpr auto F24 = Canvas::eCNAndENFont24;
constexpr auto F16 = Canvas::eCNAndENFont16;

// ---------------------------------------------------------------------------
// 布局常量
// ---------------------------------------------------------------------------
constexpr int SCREEN_W = 240;

// 关键 Y 坐标 —— 减密后单页只有 1 个 HERO(F24) + 2~3 行 secondary(F16) + 1 行 status(F16)
constexpr int HEADER_Y     = 5;     // F24 标题
constexpr int PAGE_NUM_X   = 196;   // P{n}/N 莫兰迪绿
constexpr int DIVIDER_Y1   = 31;    // 头部分隔线
constexpr int CONTENT_Y0   = 50;    // 内容起点
constexpr int DIVIDER_Y2   = 280;   // 尾部分隔线
constexpr int FOOTER_Y     = 286;   // F16 按钮提示 + idle
constexpr int SHAKE_BAR_Y  = 305;
constexpr int SHAKE_BAR_H  = 8;

// 字宽估算
static int charW(Canvas::eFontSize_t f) {
    return (f == F24) ? 14 : 9;
}
static int lineH(Canvas::eFontSize_t f) {
    return (f == F24) ? 28 : 18;
}

// ---------------------------------------------------------------------------
// PageCache
// ---------------------------------------------------------------------------
struct PageCache {
    bool    ever_drawn = false;

    // header / footer
    char    title[20]   = "";
    char    pagenum[8]  = "";
    char    idle[24]    = "";

    // P1 Overview
    char    hero_label[8]   = "";
    char    hero_value[16]  = "";
    char    metric_a[24]    = "";     // 单行二级 "Humid 58%"
    char    metric_b[24]    = "";     // "Light 240 lx"
    char    status_line[32] = "";

    // P2 AI Usage
    char    ai_title_a[20]  = "";
    char    ai_title_b[20]  = "";
    char    ai_pct_a[8]     = "";
    char    ai_pct_b[8]     = "";
    int     ai_bar_a_w      = -1;
    int     ai_bar_b_w      = -1;
    char    ai_hint[28]     = "";

    // P3 Environment
    char    env_comfort[20] = "";
    char    env_secondary[40] = "";

    // P4 Settings —— 整体块字符串
    char    settings_block[200] = "";

    // P5 Landscape Overview
    char    land_hero_l_label[8]  = "";
    char    land_hero_l_value[16] = "";
    char    land_hero_r_label[8]  = "";
    char    land_hero_r_value[16] = "";
    char    land_sub_l_label[8]   = "";
    char    land_sub_l_value[16]  = "";
    char    land_sub_r_label[8]   = "";
    char    land_sub_r_value[16]  = "";

    // SLP Face-Down
    char    fd_dot[2]  = "";
    char    fd_l1[24]  = "";
    char    fd_l2[24]  = "";
    char    fd_l3[24]  = "";

    // CFG Config
    char    cfg_label[16] = "";
    char    cfg_ssid[24]  = "";
    char    cfg_label2[16] = "";
    char    cfg_url[24]   = "";
};

static PageCache g_cache[PAGE_COUNT];
static UIPage   g_last_page = PAGE_COUNT;
static ShakePhase g_last_shake_phase = SHAKE_PHASE_IDLE;

// ---------------------------------------------------------------------------
// 底层
// ---------------------------------------------------------------------------

// drawField：变了才画；先黑矩形擦老一块再写新值
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
// 通用 header / footer
// ---------------------------------------------------------------------------

// header：标题（F24 白）+ P{n}/N（F24 莫兰迪绿）
static bool draw_header(UIPage p, const char* title) {
    PageCache& c = g_cache[p];
    bool dirty = false;
    dirty |= drawField(c.title, sizeof(c.title), title,
                       5, HEADER_Y, 14, F24, COLOR_TITLE);
    char pid[8];
    snprintf(pid, sizeof(pid), "P%u/%u", (unsigned)(p + 1), (unsigned)PAGE_COUNT);
    dirty |= drawField(c.pagenum, sizeof(c.pagenum), pid,
                       PAGE_NUM_X, HEADER_Y, 6, F24, COLOR_MORANDI);
    return dirty;
}

// footer：左边按钮提示 + 右边 idle（都是 F16）
static bool draw_footer(UIPage p, const char* hint) {
    PageCache& c = g_cache[p];
    bool dirty = false;
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y2, 235, COLOR_LINE);

    // 按钮 hint 居左
    dirty |= drawField(c.settings_block, sizeof(c.settings_block), hint,
                       5, FOOTER_Y, 20, F16, COLOR_LABEL);
    // idle 居右（跟一秒一秒自动触发更新）
    char ib[16];
    const auto& s = g_state.snapshot();
    snprintf(ib, sizeof(ib), "idle %lus",
             (unsigned long)((millis() - s.lastInputMs) / 1000));
    dirty |= drawField(c.idle, sizeof(c.idle), ib,
                       180, FOOTER_Y, 8, F16, COLOR_LABEL);
    return dirty;
}

// 状态点（F16 "●" 莫兰迪绿）
static void drawStatusDot(int x, int y) {
    k10.canvas->canvasCircle(x, y + 8, 4, COLOR_MORANDI, COLOR_BG, true);
}

// ---------------------------------------------------------------------------
// 数据辅助
// ---------------------------------------------------------------------------
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
    return COLOR_MORANDI;   // 莫兰迪绿
}

// 三段式甩动反馈（OUTBOUND/RETURN 进度条）
static bool draw_shake_transition() {
    const ShakePhase phase = g_gesture.shakePhase();
    if (phase == g_last_shake_phase) return false;
    constexpr int y = SHAKE_BAR_Y;
    constexpr int h = SHAKE_BAR_H;
    constexpr int w = SCREEN_W;
    k10.canvas->canvasRectangle(0, y, w, h, COLOR_BG, COLOR_BG, true);
    int pct = shakeAnimationPercent(phase);
    if (pct > 0) {
        const int pw = (w * pct) / 100;
        const int x = (g_gesture.shakeDirection() > 0) ? 0 : (w - pw);
        uint32_t c = (phase == SHAKE_PHASE_OUTBOUND) ? COLOR_WARN : COLOR_MORANDI;
        if (pw > 0) k10.canvas->canvasRectangle(x, y, pw, h, c, c, true);
    }
    g_last_shake_phase = phase;
    return true;
}

// ===========================================================================
// Portrait 页面（每个页面只放 1 个 HERO + 2 行 secondary + 1 行 status）
// ===========================================================================

// P1: Overview — HERO 温度 + 二级两行 + 状态一行
static bool render_portrait_overview() {
    PageCache& c = g_cache[PAGE_PORTRAIT_OVERVIEW];
    bool dirty = draw_header(PAGE_PORTRAIT_OVERVIEW, "DeskNest");
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    const auto aht  = g_sensors.aht20();
    const auto lux  = g_sensors.ltr303();
    const auto bat  = g_sensors.battery();
    const auto& st  = g_state.snapshot();

    // 小标签（90px 高度内）
    dirty |= drawField(c.hero_label, sizeof(c.hero_label),
                       "TEMP", 5, 80, 4, F16, COLOR_LABEL);

    // HERO（28px F24 + 大留白）
    char hv[16];
    if (aht.valid) snprintf(hv, sizeof(hv), "%.1f°C", aht.temperatureC);
    else           snprintf(hv, sizeof(hv), "--°C");
    dirty |= drawField(c.hero_value, sizeof(c.hero_value), hv,
                       5, 105, 12, F24, COLOR_TITLE);

    // 二级行 1：湿度
    char ma[24];
    if (aht.valid) snprintf(ma, sizeof(ma), "Humid %.0f%%", aht.humidityPct);
    else           snprintf(ma, sizeof(ma), "Humid --");
    dirty |= drawField(c.metric_a, sizeof(c.metric_a), ma,
                       5, 165, 14, F16, COLOR_LABEL);

    // 二级行 2：光照
    char mb[24];
    snprintf(mb, sizeof(mb), "Light %d lx", (int)lux.lux);
    dirty |= drawField(c.metric_b, sizeof(c.metric_b), mb,
                       5, 190, 16, F16, COLOR_LABEL);

    // 状态行（莫兰迪绿状态点 + 短状态）
    char sr[32];
    const char* orient = (st.orientation == ORIENTATION_PORTRAIT) ? "Portrait"
                       : (st.orientation == ORIENTATION_LANDSCAPE) ? "Landscape"
                       : (st.orientation == ORIENTATION_FACE_DOWN) ? "Face-Dn"
                       : "Unknown";
    if (bat.valid) snprintf(sr, sizeof(sr), "%s  ·  %d%%", orient, bat.percent);
    else           snprintf(sr, sizeof(sr), "%s  ·  USB", orient);
    if (dirty || strcmp(c.status_line, sr) != 0 || !c.ever_drawn) {
        // 莫兰迪绿小圆点 + 文字
        k10.canvas->canvasRectangle(5, 240, 230, 22, COLOR_BG, COLOR_BG, true);
        drawStatusDot(15, 240);
        k10.canvas->canvasText(sr, 25, 245, COLOR_MORANDI, F16, 50, false);
        strncpy(c.status_line, sr, sizeof(c.status_line) - 1);
        c.status_line[sizeof(c.status_line) - 1] = '\0';
        dirty = true;
    }

    dirty |= draw_footer(PAGE_PORTRAIT_OVERVIEW, "[A] Next  [B] Prev");
    dirty |= draw_shake_transition();
    return dirty;
}

// P2: AI Usage — 双进度条 + 百分比
static bool render_portrait_ai_usage() {
    PageCache& c = g_cache[PAGE_PORTRAIT_AI_USAGE];
    bool dirty = draw_header(PAGE_PORTRAIT_AI_USAGE, "AI Usage");
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    AiUsage u = mock_ai_usage(millis());

    // ChatGPT — 标题 + 百分比（右上角）+ 进度条
    dirty |= drawField(c.ai_title_a, sizeof(c.ai_title_a),
                       "ChatGPT Plus", 5, 75, 14, F24, COLOR_TITLE);
    char pa[8]; snprintf(pa, sizeof(pa), "%d%%", u.chatgpt_pct);
    dirty |= drawField(c.ai_pct_a, sizeof(c.ai_pct_a), pa,
                       195, 80, 6, F16, bar_color(u.chatgpt_pct));
    dirty |= drawBar(&c.ai_bar_a_w, u.chatgpt_pct, 5, 110, 230, 14,
                     bar_color(u.chatgpt_pct));

    // Claude
    dirty |= drawField(c.ai_title_b, sizeof(c.ai_title_b),
                       "Claude Pro", 5, 155, 14, F24, COLOR_TITLE);
    char pb[8]; snprintf(pb, sizeof(pb), "%d%%", u.claude_pct);
    dirty |= drawField(c.ai_pct_b, sizeof(c.ai_pct_b), pb,
                       195, 160, 6, F16, bar_color(u.claude_pct));
    dirty |= drawBar(&c.ai_bar_b_w, u.claude_pct, 5, 190, 230, 14,
                     bar_color(u.claude_pct));

    dirty |= drawField(c.ai_hint, sizeof(c.ai_hint),
                       "P1 · cc-switch API", 5, 230, 22, F16, COLOR_LABEL);

    dirty |= draw_footer(PAGE_PORTRAIT_AI_USAGE, "[A] Next");
    dirty |= draw_shake_transition();
    return dirty;
}

// P3: Environment — HERO + 舒适度状态点 + 二级一行
static bool render_portrait_environment() {
    PageCache& c = g_cache[PAGE_PORTRAIT_ENVIRONMENT];
    bool dirty = draw_header(PAGE_PORTRAIT_ENVIRONMENT, "Environment");
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    const auto aht = g_sensors.aht20();
    const auto lux = g_sensors.ltr303();

    dirty |= drawField(c.hero_label, sizeof(c.hero_label),
                       "TEMP", 5, 80, 4, F16, COLOR_LABEL);
    char hv[16];
    if (aht.valid) snprintf(hv, sizeof(hv), "%.1f°C", aht.temperatureC);
    else           snprintf(hv, sizeof(hv), "--°C");
    dirty |= drawField(c.hero_value, sizeof(c.hero_value), hv,
                       5, 105, 12, F24, COLOR_TITLE);

    // 舒适度状态点 + 文字（莫兰迪绿 = OK，橙 = 警告）
    if (aht.valid) {
        const bool comfort = (aht.temperatureC > 18 && aht.temperatureC < 28 &&
                              aht.humidityPct > 30 && aht.humidityPct < 70);
        char cs[20];
        snprintf(cs, sizeof(cs), "%s",
                 comfort ? "Comfort" : (aht.temperatureC < 18 ? "Cold" : "Warm"));
        if (dirty || strcmp(c.env_comfort, cs) != 0 || !c.ever_drawn) {
            k10.canvas->canvasRectangle(5, 165, 230, 22, COLOR_BG, COLOR_BG, true);
            drawStatusDot(15, 165);
            k10.canvas->canvasText(cs, 25, 170,
                comfort ? COLOR_MORANDI : COLOR_WARN, F16, 50, false);
            strncpy(c.env_comfort, cs, sizeof(c.env_comfort) - 1);
            c.env_comfort[sizeof(c.env_comfort) - 1] = '\0';
            dirty = true;
        }
    }

    // 二级：单行
    char sec[40];
    if (aht.valid) {
        snprintf(sec, sizeof(sec), "Humid %.0f%%  ·  Light %d lx",
                 aht.humidityPct, (int)lux.lux);
    } else {
        snprintf(sec, sizeof(sec), "Humid --  ·  Light %d lx", (int)lux.lux);
    }
    dirty |= drawField(c.env_secondary, sizeof(c.env_secondary), sec,
                       5, 210, 30, F16, COLOR_LABEL);

    dirty |= draw_footer(PAGE_PORTRAIT_ENVIRONMENT, "[A] Next");
    dirty |= draw_shake_transition();
    return dirty;
}

// P4: Settings — 单行 F24 列表（label + value + 箭头）
static bool render_portrait_settings() {
    PageCache& c = g_cache[PAGE_PORTRAIT_SETTINGS];
    bool dirty = draw_header(PAGE_PORTRAIT_SETTINGS, "Settings");
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    // 用整块字符串缓存（任一字段变化整块重画）
    char block[200];
    snprintf(block, sizeof(block),
        "Power   Balanced  ▸\n"
        "Sync    Battery   ▸\n"
        "Density Normal    ▸\n"
        "Rotate  Auto      ▸\n"
        "Theme   Dark      ▸");

    if (strcmp(c.settings_block, block) != 0) {
        k10.canvas->canvasRectangle(5, 70, 230, 200, COLOR_BG, COLOR_BG, true);
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
            const char* arrow = strstr(one, " ▸");
            if (arrow) {
                size_t ll = arrow - one;
                char left[24], right[16];
                if (ll >= sizeof(left)) ll = sizeof(left) - 1;
                memcpy(left, one, ll); left[ll] = '\0';
                strncpy(right, arrow + 3, sizeof(right) - 1); right[sizeof(right) - 1] = '\0';
                k10.canvas->canvasText(left, 5, y, COLOR_TITLE, F24, 50, false);
                k10.canvas->canvasText(right, 5 + ll * charW(F24), y, COLOR_MORANDI, F24, 50, false);
            } else {
                k10.canvas->canvasText(one, 5, y, COLOR_TITLE, F24, 50, false);
            }
            y += 32;
        }
        strncpy(c.settings_block, block, sizeof(c.settings_block) - 1);
        c.settings_block[sizeof(c.settings_block) - 1] = '\0';
        dirty = true;
    }

    // 红色 factory reset（单独一行，与列表底部分开）
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
// Landscape 页面
// ===========================================================================

// P5: Landscape Overview — 两列四宫格（TEMP/HUMID/LIGHT/BATT）
static bool render_landscape_overview() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_OVERVIEW];
    bool dirty = draw_header(PAGE_LANDSCAPE_OVERVIEW, "DeskNest");
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    const auto aht = g_sensors.aht20();
    const auto lux = g_sensors.ltr303();
    const auto bat = g_sensors.battery();
    const auto& st = g_state.snapshot();

    // 左列 hero：温度
    dirty |= drawField(c.land_hero_l_label, sizeof(c.land_hero_l_label),
                       "TEMP", 5, 80, 4, F16, COLOR_LABEL);
    char lv[16]; snprintf(lv, sizeof(lv), "%.1f°C", aht.valid ? aht.temperatureC : 0);
    if (!aht.valid) snprintf(lv, sizeof(lv), "--°C");
    dirty |= drawField(c.land_hero_l_value, sizeof(c.land_hero_l_value), lv,
                       5, 105, 12, F24, COLOR_TITLE);

    // 右列 hero：湿度
    dirty |= drawField(c.land_hero_r_label, sizeof(c.land_hero_r_label),
                       "HUMID", 125, 80, 5, F16, COLOR_LABEL);
    char rv[16]; snprintf(rv, sizeof(rv), "%.0f%%", aht.valid ? aht.humidityPct : 0);
    if (!aht.valid) snprintf(rv, sizeof(rv), "--%%");
    dirty |= drawField(c.land_hero_r_value, sizeof(c.land_hero_r_value), rv,
                       125, 105, 12, F24, COLOR_TITLE);

    // 左列 secondary：光照
    dirty |= drawField(c.land_sub_l_label, sizeof(c.land_sub_l_label),
                       "LIGHT", 5, 170, 5, F16, COLOR_LABEL);
    char ll[16]; snprintf(ll, sizeof(ll), "%d lx", (int)lux.lux);
    dirty |= drawField(c.land_sub_l_value, sizeof(c.land_sub_l_value), ll,
                       5, 195, 12, F24, COLOR_TITLE);

    // 右列 secondary：电量
    dirty |= drawField(c.land_sub_r_label, sizeof(c.land_sub_r_label),
                       "BATT", 125, 170, 4, F16, COLOR_LABEL);
    char lr[16];
    if (bat.valid) snprintf(lr, sizeof(lr), "%d%%", bat.percent);
    else           snprintf(lr, sizeof(lr), "USB");
    dirty |= drawField(c.land_sub_r_value, sizeof(c.land_sub_r_value), lr,
                       125, 195, 12, F24, COLOR_TITLE);

    // 状态行（莫兰迪绿）
    char sr[32];
    const char* orient = (st.orientation == ORIENTATION_PORTRAIT) ? "Portrait"
                       : (st.orientation == ORIENTATION_LANDSCAPE) ? "Landscape"
                       : (st.orientation == ORIENTATION_FACE_DOWN) ? "Face-Dn"
                       : "Unknown";
    snprintf(sr, sizeof(sr), "%s", orient);
    if (dirty || strcmp(c.status_line, sr) != 0 || !c.ever_drawn) {
        k10.canvas->canvasRectangle(5, 245, 230, 22, COLOR_BG, COLOR_BG, true);
        drawStatusDot(15, 245);
        k10.canvas->canvasText(sr, 25, 250, COLOR_MORANDI, F16, 50, false);
        strncpy(c.status_line, sr, sizeof(c.status_line) - 1);
        c.status_line[sizeof(c.status_line) - 1] = '\0';
        dirty = true;
    }

    dirty |= draw_footer(PAGE_LANDSCAPE_OVERVIEW, "[A] Next  [B] Prev");
    dirty |= draw_shake_transition();
    return dirty;
}

// P6: Landscape Focus — P1 专注计时占位（横向 AI 用量）
static bool render_landscape_focus() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_FOCUS];
    bool dirty = draw_header(PAGE_LANDSCAPE_FOCUS, "Focus");
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    AiUsage u = mock_ai_usage(millis());

    dirty |= drawField(c.ai_title_a, sizeof(c.ai_title_a),
                       "ChatGPT", 5, 75, 8, F24, COLOR_TITLE);
    char pa[8]; snprintf(pa, sizeof(pa), "%d%%", u.chatgpt_pct);
    dirty |= drawField(c.ai_pct_a, sizeof(c.ai_pct_a), pa,
                       195, 80, 6, F16, bar_color(u.chatgpt_pct));
    dirty |= drawBar(&c.ai_bar_a_w, u.chatgpt_pct, 5, 110, 230, 14,
                     bar_color(u.chatgpt_pct));

    dirty |= drawField(c.ai_title_b, sizeof(c.ai_title_b),
                       "Claude", 5, 150, 8, F24, COLOR_TITLE);
    char pb[8]; snprintf(pb, sizeof(pb), "%d%%", u.claude_pct);
    dirty |= drawField(c.ai_pct_b, sizeof(c.ai_pct_b), pb,
                       195, 155, 6, F16, bar_color(u.claude_pct));
    dirty |= drawBar(&c.ai_bar_b_w, u.claude_pct, 5, 185, 230, 14,
                     bar_color(u.claude_pct));

    dirty |= drawField(c.ai_hint, sizeof(c.ai_hint),
                       "P1: focus timer", 5, 225, 18, F16, COLOR_LABEL);

    dirty |= draw_footer(PAGE_LANDSCAPE_FOCUS, "[A] Next  [B] Prev");
    dirty |= draw_shake_transition();
    return dirty;
}

// P7: Landscape Custom — 居中卡片占位
static bool render_landscape_custom() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_CUSTOM];
    bool dirty = draw_header(PAGE_LANDSCAPE_CUSTOM, "Custom");
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    if (!c.ever_drawn) {
        k10.canvas->canvasRectangle(40, 110, 160, 80, COLOR_CARD, COLOR_CARD, true);
        k10.canvas->canvasRectangle(40, 110, 160, 1, COLOR_MORANDI_D, COLOR_MORANDI_D, true);
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

    if (!c.ever_drawn) {
        k10.canvas->canvasClear();
        c.ever_drawn = true;
        dirty = true;
    }

    // 居中 3 行 + 莫兰迪绿小点
    drawStatusDot(116, 95);
    dirty |= drawField(c.fd_l1, sizeof(c.fd_l1),
                       "Perched on desk,", 40, 135, 18, F24, COLOR_LABEL);
    dirty |= drawField(c.fd_l2, sizeof(c.fd_l2),
                       "dormant between", 30, 165, 18, F24, COLOR_LABEL);
    dirty |= drawField(c.fd_l3, sizeof(c.fd_l3),
                       "wake-ups.", 75, 195, 12, F24, COLOR_LABEL);

    return dirty;
}

// CFG: WiFi 配网
static bool render_config_portal() {
    PageCache& c = g_cache[PAGE_CONFIG_PORTAL];
    bool dirty = draw_header(PAGE_CONFIG_PORTAL, "WiFi Setup");
    drawOnceLine(&c.ever_drawn, 5, DIVIDER_Y1, 235, COLOR_LINE);

    dirty |= drawField(c.cfg_label, sizeof(c.cfg_label),
                       "Connect to", 5, 80, 12, F24, COLOR_TITLE);
    dirty |= drawField(c.cfg_ssid, sizeof(c.cfg_ssid),
                       "DeskNest-XXXX", 5, 115, 16, F24, COLOR_MORANDI);

    dirty |= drawField(c.cfg_label2, sizeof(c.cfg_label2),
                       "Open browser", 5, 170, 14, F24, COLOR_TITLE);
    dirty |= drawField(c.cfg_url, sizeof(c.cfg_url),
                       "192.168.4.1", 5, 205, 14, F24, COLOR_MORANDI);

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
    Serial.println("[D][UI] Morandi theme ready (240x320)");

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