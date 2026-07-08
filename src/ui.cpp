// src/ui.cpp
// 栖屏 DeskNest - UI 渲染层（莫兰迪绿主题 · 放大版 v3）
// "栖于桌面，息于常亮之间"
//
// v3 调整：
//   1. **字号升级** —— F24 作为主字号贯穿全屏；F16 仅用于次要 hint
//   2. **每页元素精简** —— 去掉冗余装饰条/提示卡，只保留核心信息
//   3. **横屏方向感** —— P5/P6/P7 加 LANDSCAPE 标识 + 横向铺开布局
//   4. **节奏舒缓** —— 行间距 ≥ 32px，呼吸感更强
//   5. **统一 header/footer** —— F16 title + F16 P{n}/N(莫兰迪绿)
//   6. **脏区局部刷新** —— canvasClear 仅翻页；同页字段级 diff

#include "ui.h"
#include "config.h"
#include "ui_model.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <unihiker_k10.h>

namespace desknest {

extern UNIHIKER_K10 k10;

namespace {

// ---------------------------------------------------------------------------
// 莫兰迪调色板
// ---------------------------------------------------------------------------
constexpr uint32_t COLOR_BG       = 0x0F1419;
constexpr uint32_t COLOR_CARD     = 0x1A2128;
constexpr uint32_t COLOR_LINE     = 0x2A3447;

constexpr uint32_t COLOR_TITLE    = 0xECE8DC;
constexpr uint32_t COLOR_LABEL    = 0x8B95A8;
constexpr uint32_t COLOR_DIM      = 0x5A6478;

constexpr uint32_t COLOR_MORANDI  = 0x9CB89B;   // ★ 主色
constexpr uint32_t COLOR_MORANDI_D= 0x7A9A7B;

constexpr uint32_t COLOR_SAND     = 0xD4B896;   // 莫兰迪沙
constexpr uint32_t COLOR_CREAM    = 0xDCD4C0;   // 莫兰迪米白
constexpr uint32_t COLOR_BLUE     = 0xA8B5C4;   // 莫兰迪雾蓝
constexpr uint32_t COLOR_PINK     = 0xD4A5A5;   // 莫兰迪粉

constexpr uint32_t COLOR_NORMAL   = 0x9CB89B;
constexpr uint32_t COLOR_WARN     = 0xD4B896;
constexpr uint32_t COLOR_ALERT    = 0xC97C7C;

constexpr uint32_t RGB_OFF        = 0x000000;

constexpr auto F24 = Canvas::eCNAndENFont24;
constexpr auto F16 = Canvas::eCNAndENFont16;

// ---------------------------------------------------------------------------
// 布局常量（240×320）
// ---------------------------------------------------------------------------
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 320;

// Header（Y=8~30，F16=18px）
constexpr int HEADER_Y     = 8;
constexpr int DIVIDER_Y1   = 30;
// 内容区：Y=42 ~ 280（可用 238px）
// Footer（Y=286~310）
constexpr int DIVIDER_Y2   = 286;
constexpr int FOOTER_Y     = 292;
constexpr int SHAKE_BAR_Y  = 308;
constexpr int SHAKE_BAR_H  = 8;

// 页内 dot 导航（mini 横条，紧贴 header divider 上方）
constexpr int DOT_NAV_Y    = 22;   // Y=22 距 divider Y=30 留 8px 净空
constexpr int DOT_NAV_W    = 12;
constexpr int DOT_NAV_H    = 2;
constexpr int DOT_NAV_GAP  = 6;

// 字宽估算
//   F24: 半角 ~12px, 全角 ~24px
//   F16: 半角 ~9px, 全角 ~16px
// charW 返回"半角字符宽度"; 全角字符按半角的 2 倍算
static int charW(Canvas::eFontSize_t f) {
    return (f == F24) ? 12 : 9;
}
static int lineH(Canvas::eFontSize_t f) {
    return (f == F24) ? 28 : 18;
}

// ---------------------------------------------------------------------------
// PageCache
// ---------------------------------------------------------------------------
struct PageCache {
    bool ever_drawn = false;

    char title[20]   = "";
    char pagenum[8]  = "";
    char hint[28]    = "";
    char idle[16]    = "";

    // P1 Overview
    char p1_label[8]    = "";      // F16 "温度"
    char p1_hero[16]    = "";      // F24 "23.5°C"
    char p1_metric_a[16]= "";      // F24 "湿 58%"
    char p1_metric_b[16]= "";      // F24 "光 240lx"
    char p1_metric_c[16]= "";      // F24 "电 78%"
    char p1_status[24]  = "";      // F24 "Portrait · ACTIVE"

    // P2 AI Usage
    char p2_title_a[20] = "";      // F24 "ChatGPT Plus"
    char p2_title_b[20] = "";      // F24 "Claude Pro"
    char p2_pct_a[8]    = "";      // F24 "72%"
    char p2_pct_b[8]    = "";
    int  p2_bar_a_w     = -1;
    int  p2_bar_b_w     = -1;
    char p2_hint[24]    = "";      // F16 "cc-switch · 22 min"

    // P3 Environment
    char p3_score[16]   = "";      // F24 "82 / 100"
    char p3_grade[12]   = "";      // F24 "良好"
    char p3_axis[40]    = "";      // F16 "T 23.5°C  H 58%  L 240lx"
    char p3_advice[20]  = "";      // F24 "保持专注"
    int  p3_score_w     = -1;

    // P4 Settings
    char p4_block[400]  = "";
    char p4_factory[28] = "";

    // P5 Landscape Overview（横向铺开两列四宫格）
    char p5_lab_l[12]   = "";      // F16 "TEMP"
    char p5_val_l[12]   = "";      // F24 "23.5°C"
    char p5_lab_r[12]   = "";
    char p5_val_r[12]   = "";
    char p5_lab_l2[12]  = "";
    char p5_val_l2[12]  = "";
    char p5_lab_r2[12]  = "";
    char p5_val_r2[12]  = "";
    char p5_mode[16]    = "";      // F16 "> LANDSCAPE"

    // P6 Landscape Focus
    char p6_mode[16]    = "";      // F24 "DEEP WORK"
    char p6_time[16]    = "";      // F24 "25:00"
    char p6_state[20]   = "";      // F24 "● IN PROGRESS"
    char p6_sub[24]     = "";      // F16 "Goal · 50 min"

    // P7 Landscape Custom
    char p7_a[8]        = "";      // F24 "WORK"
    char p7_b[8]        = "";      // F24 "REST"
    char p7_c[8]        = "";      // F24 "MEET"
    char p7_hint[24]    = "";      // F16 "Tap to switch mode"
    int  p7_active      = -1;

    // P8 Face-Down
    char p8_l1[20]      = "";
    char p8_l2[20]      = "";
    char p8_l3[20]      = "";

    // P9 Config Portal
    char p9_label1[20]  = "";
    char p9_ssid[24]    = "";
    char p9_label2[20]  = "";
    char p9_url[24]     = "";
    char p9_hint[24]    = "";
    char p9_hint2[28]   = "";
    char p9_hint3[24]   = "";
};

static PageCache g_cache[PAGE_COUNT];
static UIPage   g_last_page = PAGE_COUNT;
static UiModel  g_model;
static ShakeVisualPhase g_last_shake_phase = SHAKE_VISUAL_IDLE;

// ---------------------------------------------------------------------------
// 底层
// ---------------------------------------------------------------------------

// drawField：变了才画；按字符数上限擦老一块再写
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

// drawFieldFit：自定义擦除宽度（用于长字符串）
static bool drawFieldFit(char* cache, size_t cap, const char* new_val,
                         int x, int y, int wipe_w,
                         Canvas::eFontSize_t font, uint32_t color) {
    if (strcmp(cache, new_val) == 0) return false;
    int h = lineH(font);
    k10.canvas->canvasRectangle(x, y, wipe_w, h, COLOR_BG, COLOR_BG, true);
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
    k10.canvas->canvasRectangle(x, y, w, 1, COLOR_LINE, COLOR_LINE, true);
    k10.canvas->canvasRectangle(x, y + h - 1, w, 1, COLOR_LINE, COLOR_LINE, true);
    if (new_w > 0) k10.canvas->canvasRectangle(x, y, new_w, h, bar_c, bar_c, true);
    *cache_w = new_w;
    return true;
}

static bool drawScoreBar(int* cache_w, int new_pct,
                         int x, int y, int w, int h,
                         uint32_t bg_c, uint32_t bar_c) {
    int new_w = (w * new_pct) / 100;
    if (new_w < 0) new_w = 0;
    if (*cache_w == new_w) return false;
    k10.canvas->canvasRectangle(x, y, w, h, bg_c, bg_c, true);
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

// 短色条
static void drawAccentBar(int x, int y, int w, uint32_t color) {
    k10.canvas->canvasRectangle(x, y, w, 3, color, color, true);
}

// 状态点
static void drawDot(int x, int y, uint32_t color = COLOR_MORANDI, int r = 4) {
    k10.canvas->canvasCircle(x, y, r, color, COLOR_BG, true);
}

// 页内 dot 导航：按 orientation 分组（竖 4 / 横 3），P8 P9 不画
// 当前页索引高亮 COLOR_MORANDI，其他 PAGE_LINE
static void drawDotNav(UIPage p, OrientationState orient) {
    int count = 0;
    int active = -1;
    if (orient == ORIENTATION_LANDSCAPE &&
        (p == PAGE_LANDSCAPE_OVERVIEW ||
         p == PAGE_LANDSCAPE_FOCUS    ||
         p == PAGE_LANDSCAPE_CUSTOM)) {
        count = 3;
        active = (int)p - (int)PAGE_LANDSCAPE_OVERVIEW;
    } else if (orient != ORIENTATION_LANDSCAPE &&
               (p == PAGE_PORTRAIT_OVERVIEW  ||
                p == PAGE_PORTRAIT_AI_USAGE  ||
                p == PAGE_PORTRAIT_ENVIRONMENT ||
                p == PAGE_PORTRAIT_SETTINGS)) {
        count = 4;
        active = (int)p - (int)PAGE_PORTRAIT_OVERVIEW;
    }
    if (count == 0) return;

    const int total_w = count * DOT_NAV_W + (count - 1) * DOT_NAV_GAP;
    int x = (SCREEN_W - total_w) / 2;
    for (int i = 0; i < count; ++i) {
        uint32_t c = (i == active) ? COLOR_MORANDI : COLOR_LINE;
        k10.canvas->canvasRectangle(x, DOT_NAV_Y, DOT_NAV_W, DOT_NAV_H,
                                    c, c, true);
        x += DOT_NAV_W + DOT_NAV_GAP;
    }
}

// ---------------------------------------------------------------------------
// 通用 header / footer
// ---------------------------------------------------------------------------

static bool draw_header(UIPage p, const char* title) {
    PageCache& c = g_cache[p];
    bool dirty = false;

    dirty |= drawField(c.title, sizeof(c.title), title,
                       5, HEADER_Y, 18, F16, COLOR_TITLE);

    char pid[8];
    snprintf(pid, sizeof(pid), "P%u/%u", (unsigned)(p + 1), (unsigned)PAGE_COUNT);
    dirty |= drawField(c.pagenum, sizeof(c.pagenum), pid,
                       SCREEN_W - 5 - 6 * charW(F16), HEADER_Y, 6, F16, COLOR_MORANDI);

    if (!c.ever_drawn) {
        drawAccentBar(5, 26, 20, COLOR_MORANDI);
        c.ever_drawn = true;
    }
    return dirty;
}

static void draw_header_divider() {
    k10.canvas->canvasLine(5, DIVIDER_Y1, SCREEN_W - 5, DIVIDER_Y1, COLOR_LINE);
    // dot 导航在 divider 上方 8px，按当前 UiModel 高亮
    drawDotNav(g_model.view.page, g_model.view.orientation);
}

static bool draw_footer(UIPage p, const char* hint) {
    PageCache& c = g_cache[p];
    bool dirty = false;
    k10.canvas->canvasLine(5, DIVIDER_Y2, SCREEN_W - 5, DIVIDER_Y2, COLOR_LINE);

    if (hint && *hint) {
        dirty |= drawField(c.hint, sizeof(c.hint), hint,
                           5, FOOTER_Y, 20, F16, COLOR_LABEL);
    }

    char ib[16];
    snprintf(ib, sizeof(ib), "idle %lus",
             (unsigned long)g_model.view.idleSeconds);
    dirty |= drawFieldFit(c.idle, sizeof(c.idle), ib,
                          SCREEN_W - 80, FOOTER_Y, 75, F16, COLOR_DIM);
    return dirty;
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

const char* orient_str(OrientationState o) {
    switch (o) {
        case ORIENTATION_PORTRAIT:  return "Portrait";
        case ORIENTATION_LANDSCAPE: return "Landscape";
        case ORIENTATION_FACE_DOWN: return "Face-Dn";
        default:                    return "Unknown";
    }
}

uint32_t bar_color(int pct) {
    if (pct > 85) return COLOR_ALERT;
    if (pct > 50) return COLOR_WARN;
    return COLOR_MORANDI;
}

uint32_t state_color(const char* grade) {
    if (!grade) return COLOR_DIM;
    if (strcmp(grade, "OK") == 0)     return COLOR_MORANDI;
    if (strcmp(grade, "良好") == 0)   return COLOR_MORANDI;
    if (strcmp(grade, "偏冷") == 0)   return COLOR_BLUE;
    if (strcmp(grade, "偏热") == 0)   return COLOR_SAND;
    if (strcmp(grade, "干燥") == 0)   return COLOR_SAND;
    if (strcmp(grade, "潮湿") == 0)   return COLOR_BLUE;
    if (strcmp(grade, "偏暗") == 0)   return COLOR_SAND;
    if (strcmp(grade, "偏亮") == 0)   return COLOR_BLUE;
    if (strcmp(grade, "舒适") == 0)   return COLOR_MORANDI;
    if (strcmp(grade, "一般") == 0)   return COLOR_SAND;
    if (strcmp(grade, "欠佳") == 0)   return COLOR_ALERT;
    return COLOR_DIM;
}

static bool draw_shake_transition() {
    const ShakeVisualPhase phase = g_model.animation.shakePhase;
    if (phase == g_last_shake_phase) return false;
    constexpr int y = SHAKE_BAR_Y;
    constexpr int h = SHAKE_BAR_H;
    constexpr int w = SCREEN_W;
    k10.canvas->canvasRectangle(0, y, w, h, COLOR_BG, COLOR_BG, true);
    int pct = g_model.animation.shakeProgressPct;
    if (pct > 0) {
        const int pw = (w * pct) / 100;
        const int x = (g_model.animation.shakeDirection > 0) ? 0 : (w - pw);
        uint32_t c = (phase == SHAKE_VISUAL_OUTBOUND) ? COLOR_SAND : COLOR_MORANDI;
        if (pw > 0) k10.canvas->canvasRectangle(x, y, pw, h, c, c, true);
    }
    g_last_shake_phase = phase;
    return true;
}

// ===========================================================================
// Portrait 页面
// ===========================================================================

// ---------- P1: Overview —— 大盘（3 metric + 状态）----------
static bool render_portrait_overview() {
    PageCache& c = g_cache[PAGE_PORTRAIT_OVERVIEW];
    bool dirty = draw_header(PAGE_PORTRAIT_OVERVIEW, "DeskNest");
    draw_header_divider();

    const auto& ov = g_model.overview;
    const auto& status = g_model.status;

    // label (F16)
    dirty |= drawField(c.p1_label, sizeof(c.p1_label),
                       "温度", 5, 50, 4, F16, COLOR_LABEL);

    // HERO (F24)
    char hv[16];
    if (ov.environmentValid) snprintf(hv, sizeof(hv), "%.1f°C", ov.temperatureC);
    else           snprintf(hv, sizeof(hv), "--°C");
    dirty |= drawField(c.p1_hero, sizeof(c.p1_hero), hv,
                       5, 72, 12, F24, COLOR_TITLE);

    // 莫兰迪短装饰条
    if (!c.ever_drawn) {
        drawAccentBar(5, 104, 40, COLOR_MORANDI);
    }

    // 三 metric 行 (F24，紧凑纵向铺开)
    char ma[16];
    if (ov.environmentValid) snprintf(ma, sizeof(ma), "湿 %.0f%%", ov.humidityPct);
    else           snprintf(ma, sizeof(ma), "湿 --");
    dirty |= drawField(c.p1_metric_a, sizeof(c.p1_metric_a), ma,
                       5, 130, 12, F24, COLOR_TITLE);

    char mb[16];
    snprintf(mb, sizeof(mb), "光 %d lx", (int)ov.lux);
    dirty |= drawField(c.p1_metric_b, sizeof(c.p1_metric_b), mb,
                       5, 168, 12, F24, COLOR_TITLE);

    char mc[16];
    if (status.batteryValid) snprintf(mc, sizeof(mc), "电 %d%%", status.batteryPercent);
    else           snprintf(mc, sizeof(mc), "电 USB");
    dirty |= drawField(c.p1_metric_c, sizeof(c.p1_metric_c), mc,
                       5, 206, 12, F24, COLOR_TITLE);

    // 状态条（F24 莫兰迪绿点 + 文字）
    char sr[24];
    snprintf(sr, sizeof(sr), "%s · %s",
             status.orientationText, status.systemText);
    if (dirty || strcmp(c.p1_status, sr) != 0 || !c.ever_drawn) {
        k10.canvas->canvasRectangle(5, 244, 230, 32, COLOR_BG, COLOR_BG, true);
        drawDot(18, 260, COLOR_MORANDI, 4);
        k10.canvas->canvasText(sr, 30, 248, COLOR_MORANDI, F24, 50, false);
        strncpy(c.p1_status, sr, sizeof(c.p1_status) - 1);
        c.p1_status[sizeof(c.p1_status) - 1] = '\0';
        dirty = true;
    }

    dirty |= draw_footer(PAGE_PORTRAIT_OVERVIEW, "[A] Next  [B] Prev");
    dirty |= draw_shake_transition();
    return dirty;
}

// ---------- P2: AI Usage —— 双进度条（F24 大百分比）----------
static bool render_portrait_ai_usage() {
    PageCache& c = g_cache[PAGE_PORTRAIT_AI_USAGE];
    bool dirty = draw_header(PAGE_PORTRAIT_AI_USAGE, "AI Usage");
    draw_header_divider();

    const auto& usage = g_model.aiUsage;

    // ChatGPT Plus
    dirty |= drawField(c.p2_title_a, sizeof(c.p2_title_a),
                       "ChatGPT", 5, 72, 16, F24, COLOR_LABEL);

    char pa[8]; snprintf(pa, sizeof(pa), "%d%%", usage.chatgpt.percent);
    int pct_x = SCREEN_W - 5 - strlen(pa) * charW(F24);
    dirty |= drawField(c.p2_pct_a, sizeof(c.p2_pct_a), pa,
                       pct_x, 72, 6, F24, bar_color(usage.chatgpt.percent));

    dirty |= drawBar(&c.p2_bar_a_w, usage.chatgpt.percent, 5, 104, 230, 16,
                     bar_color(usage.chatgpt.percent));

    // Claude Pro
    dirty |= drawField(c.p2_title_b, sizeof(c.p2_title_b),
                       "Claude", 5, 144, 16, F24, COLOR_LABEL);

    char pb[8]; snprintf(pb, sizeof(pb), "%d%%", usage.codex.percent);
    pct_x = SCREEN_W - 5 - strlen(pb) * charW(F24);
    dirty |= drawField(c.p2_pct_b, sizeof(c.p2_pct_b), pb,
                       pct_x, 144, 6, F24, bar_color(usage.codex.percent));

    dirty |= drawBar(&c.p2_bar_b_w, usage.codex.percent, 5, 176, 230, 16,
                     bar_color(usage.codex.percent));

    // 提示（莫兰迪蓝 F16）
    dirty |= drawField(c.p2_hint, sizeof(c.p2_hint),
                       "via cc-switch", 5, 234, 18, F16, COLOR_BLUE);

    dirty |= draw_footer(PAGE_PORTRAIT_AI_USAGE, "[A] Next");
    dirty |= draw_shake_transition();
    return dirty;
}

// ---------- P3: Environment —— 舒适度评分 ----------
static bool render_portrait_environment() {
    PageCache& c = g_cache[PAGE_PORTRAIT_ENVIRONMENT];
    bool dirty = draw_header(PAGE_PORTRAIT_ENVIRONMENT, "Environment");
    draw_header_divider();

    const auto& env = g_model.environment;
    const int score = env.score;
    const char* grade = env.gradeText;
    const char* t_grade = env.temperatureGrade;
    const char* h_grade = env.humidityGrade;
    const char* l_grade = env.lightGrade;

    // HERO (F24 "82 / 100")
    char sc[16]; snprintf(sc, sizeof(sc), "%d / 100", score);
    dirty |= drawField(c.p3_score, sizeof(c.p3_score), sc,
                       5, 56, 14, F24, state_color(grade));

    // 等级 (F24)
    dirty |= drawField(c.p3_grade, sizeof(c.p3_grade), grade,
                       5, 96, 8, F24, state_color(grade));

    // 评分条
    dirty |= drawScoreBar(&c.p3_score_w, score,
                          5, 130, 230, 10,
                          COLOR_LINE, state_color(grade));

    // 细分隔
    if (!c.ever_drawn) {
        k10.canvas->canvasLine(5, 152, SCREEN_W - 5, 152, COLOR_LINE);
    }

    // 三轴紧凑（F16 单行 F16，全宽 < 26 字符）
    char axis[40];
    if (env.valid) {
        snprintf(axis, sizeof(axis), "T %.0f  H %.0f  L %d",
                 env.temperatureC, env.humidityPct, (int)env.lux);
    } else {
        snprintf(axis, sizeof(axis), "T --  H --  L --");
    }
    dirty |= drawField(c.p3_axis, sizeof(c.p3_axis), axis,
                       5, 170, 26, F16, COLOR_TITLE);

    // 副标（F16 莫兰迪蓝，三轴评级）
    char grades[40];
    snprintf(grades, sizeof(grades), "T:%s  H:%s  L:%s", t_grade, h_grade, l_grade);
    if (!c.ever_drawn) {
        k10.canvas->canvasText(grades, 5, 196, COLOR_BLUE, F16, 50, false);
    }

    // 建议 (F24 莫兰迪沙)
    const char* adv = env.adviceText;
    dirty |= drawField(c.p3_advice, sizeof(c.p3_advice), adv,
                       5, 234, 12, F24, COLOR_SAND);

    dirty |= draw_footer(PAGE_PORTRAIT_ENVIRONMENT, "[A] Next");
    dirty |= draw_shake_transition();
    return dirty;
}

// ---------- P4: Settings —— 列表（F24 一行 36px）----------
static bool render_portrait_settings() {
    PageCache& c = g_cache[PAGE_PORTRAIT_SETTINGS];
    bool dirty = draw_header(PAGE_PORTRAIT_SETTINGS, "Settings");
    draw_header_divider();

    char block[400];
    snprintf(block, sizeof(block),
        "Power|Balanced|\n"
        "Sync|Battery|\n"
        "Density|Normal|\n"
        "Rotate|Auto|\n"
        "Theme|Dark|");

    if (strcmp(c.p4_block, block) != 0) {
        k10.canvas->canvasRectangle(5, 50, 230, 200, COLOR_BG, COLOR_BG, true);

        int y = 50;
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
            const char* p1 = strchr(one, '|');
            if (p1) {
                char label[16] = {0}, value[24] = {0};
                size_t ll = p1 - one;
                if (ll >= sizeof(label)) ll = sizeof(label) - 1;
                memcpy(label, one, ll); label[ll] = '\0';
                const char* p2 = strchr(p1 + 1, '|');
                if (p2) {
                    size_t vl = p2 - (p1 + 1);
                    if (vl >= sizeof(value)) vl = sizeof(value) - 1;
                    memcpy(value, p1 + 1, vl); value[vl] = '\0';
                }
                // label (左，F24)
                k10.canvas->canvasText(label, 5, y, COLOR_LABEL, F24, 50, false);
                // 分隔点
                k10.canvas->canvasCircle(95, y + 12, 2, COLOR_DIM, COLOR_BG, true);
                // value (中，F24)
                k10.canvas->canvasText(value, 110, y, COLOR_TITLE, F24, 50, false);
                // 箭头（右，F24 莫兰迪绿）
                k10.canvas->canvasText(">", 218, y, COLOR_MORANDI, F24, 50, false);
            }
            y += 36;
        }
        strncpy(c.p4_block, block, sizeof(c.p4_block) - 1);
        c.p4_block[sizeof(c.p4_block) - 1] = '\0';
        dirty = true;
    }

    // 细分隔
    if (!c.ever_drawn) {
        k10.canvas->canvasLine(5, 244, SCREEN_W - 5, 244, COLOR_LINE);
    }

    dirty |= drawField(c.p4_factory, sizeof(c.p4_factory),
                       "[A+B] Factory", 5, 254, 18, F24, COLOR_ALERT);

    dirty |= draw_footer(PAGE_PORTRAIT_SETTINGS, "");
    dirty |= draw_shake_transition();
    return dirty;
}

// ===========================================================================
// Landscape 页面 —— 在 240×320 画布上"横向铺开"两列/三列
// ===========================================================================

// ---------- P5: Landscape Overview —— 两列四宫格 + LANDSCAPE 标识 ----------
// 横屏页面"假装宽屏"：用两列横向并排四宫格，强调横屏感
static bool render_landscape_overview() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_OVERVIEW];
    bool dirty = draw_header(PAGE_LANDSCAPE_OVERVIEW, "DeskNest");
    draw_header_divider();

    const auto& land = g_model.landscapeOverview;
    const auto& status = g_model.status;

    // 第一行：TEMP / HUMID
    dirty |= drawField(c.p5_lab_l, sizeof(c.p5_lab_l),
                       "TEMP", 8, 72, 4, F16, COLOR_LABEL);
    char lv[12];
    if (land.environmentValid) snprintf(lv, sizeof(lv), "%.1f°C", land.temperatureC);
    else           snprintf(lv, sizeof(lv), "--°C");
    dirty |= drawField(c.p5_val_l, sizeof(c.p5_val_l), lv,
                       8, 94, 12, F24, COLOR_TITLE);

    // 中央纵向分隔
    if (!c.ever_drawn) {
        k10.canvas->canvasLine(120, 70, 120, 224, COLOR_LINE);
    }

    dirty |= drawField(c.p5_lab_r, sizeof(c.p5_lab_r),
                       "HUMID", 128, 72, 5, F16, COLOR_LABEL);
    char rv[12];
    if (land.environmentValid) snprintf(rv, sizeof(rv), "%.0f%%", land.humidityPct);
    else           snprintf(rv, sizeof(rv), "--");
    dirty |= drawFieldFit(c.p5_val_r, sizeof(c.p5_val_r), rv,
                           128, 94, 92, F24, COLOR_TITLE);

    // 细分隔
    if (!c.ever_drawn) {
        k10.canvas->canvasLine(5, 144, SCREEN_W - 5, 144, COLOR_LINE);
    }

    // 第二行：LIGHT / BATT
    dirty |= drawField(c.p5_lab_l2, sizeof(c.p5_lab_l2),
                       "LIGHT", 8, 156, 5, F16, COLOR_LABEL);
    char lv2[12];
    snprintf(lv2, sizeof(lv2), "%d lx", (int)land.lux);
    dirty |= drawField(c.p5_val_l2, sizeof(c.p5_val_l2), lv2,
                       8, 178, 12, F24, COLOR_TITLE);

    dirty |= drawField(c.p5_lab_r2, sizeof(c.p5_lab_r2),
                       "BATT", 128, 156, 4, F16, COLOR_LABEL);
    char rv2[12];
    if (status.batteryValid) snprintf(rv2, sizeof(rv2), "%d%%", status.batteryPercent);
    else           snprintf(rv2, sizeof(rv2), "USB");
    dirty |= drawFieldFit(c.p5_val_r2, sizeof(c.p5_val_r2), rv2,
                           128, 178, 92, F24, COLOR_TITLE);

    // 莫兰迪短装饰条（横屏主色强调）
    if (!c.ever_drawn) {
        drawAccentBar(8, 216, 24, COLOR_MORANDI);
        drawAccentBar(128, 216, 24, COLOR_BLUE);
    }

    // 状态条
    char sr[16];
    snprintf(sr, sizeof(sr), "%s", land.systemText);
    if (dirty || strcmp(c.p5_mode, sr) != 0 || !c.ever_drawn) {
        k10.canvas->canvasRectangle(5, 240, 230, 32, COLOR_BG, COLOR_BG, true);
        drawDot(18, 256, COLOR_MORANDI, 4);
        k10.canvas->canvasText(sr, 30, 244, COLOR_MORANDI, F24, 50, false);
        strncpy(c.p5_mode, sr, sizeof(c.p5_mode) - 1);
        c.p5_mode[sizeof(c.p5_mode) - 1] = '\0';
        dirty = true;
    }

    dirty |= draw_footer(PAGE_LANDSCAPE_OVERVIEW, "[A] Next  [B] Prev");
    dirty |= draw_shake_transition();
    return dirty;
}

// ---------- P6: Landscape Focus —— 专注计时 ----------
static bool render_landscape_focus() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_FOCUS];
    bool dirty = draw_header(PAGE_LANDSCAPE_FOCUS, "Focus");
    draw_header_divider();
    const auto& focus = g_model.focus;

    // LANDSCAPE 标识
    if (!c.ever_drawn) {
        k10.canvas->canvasRectangle(5, 42, 230, 22, COLOR_CARD, COLOR_CARD, true);
        k10.canvas->canvasRectangle(5, 42, 3, 22, COLOR_BLUE, COLOR_BLUE, true);
        k10.canvas->canvasText("> LANDSCAPE", 16, 45, COLOR_BLUE, F16, 50, false);
    }

    // 模式（F24）
    dirty |= drawField(c.p6_mode, sizeof(c.p6_mode),
                       focus.modeText, 5, 80, 10, F24, COLOR_MORANDI);

    // 计时器（F24 居中大数字）—— 限制 6 字符宽度
    dirty |= drawField(c.p6_time, sizeof(c.p6_time),
                       focus.timerText, 5, 116, 8, F24, COLOR_TITLE);

    // 状态
    dirty |= drawField(c.p6_state, sizeof(c.p6_state),
                       focus.stateText, 5, 162, 14, F24, COLOR_MORANDI);

    // 副标
    dirty |= drawField(c.p6_sub, sizeof(c.p6_sub),
                       focus.goalText, 5, 210, 18, F16, COLOR_BLUE);

    // 提示条（莫兰迪沙）
    if (!c.ever_drawn) {
        k10.canvas->canvasRectangle(5, 240, 230, 32, COLOR_CARD, COLOR_CARD, true);
        k10.canvas->canvasRectangle(5, 240, 3, 32, COLOR_SAND, COLOR_SAND, true);
        k10.canvas->canvasText("Click to start", 16, 248, COLOR_SAND, F16, 50, false);
    }

    dirty |= draw_footer(PAGE_LANDSCAPE_FOCUS, "[A] Next  [B] Prev");
    dirty |= draw_shake_transition();
    return dirty;
}

// ---------- P7: Landscape Custom —— 三场景大卡（F24 标题）----------
static bool render_landscape_custom() {
    PageCache& c = g_cache[PAGE_LANDSCAPE_CUSTOM];
    bool dirty = draw_header(PAGE_LANDSCAPE_CUSTOM, "Custom");
    draw_header_divider();
    const auto& custom = g_model.customPage;

    // LANDSCAPE 标识
    if (!c.ever_drawn) {
        k10.canvas->canvasRectangle(5, 42, 230, 22, COLOR_CARD, COLOR_CARD, true);
        k10.canvas->canvasRectangle(5, 42, 3, 22, COLOR_BLUE, COLOR_BLUE, true);
        k10.canvas->canvasText("> LANDSCAPE", 16, 45, COLOR_BLUE, F16, 50, false);
    }

    // 三场景标签 (F24 大字)
    dirty |= drawField(c.p7_a, sizeof(c.p7_a), custom.cards[0].label, 14, 90, 4, F24, COLOR_TITLE);
    dirty |= drawField(c.p7_b, sizeof(c.p7_b), custom.cards[1].label, 95, 90, 4, F24, COLOR_TITLE);
    dirty |= drawField(c.p7_c, sizeof(c.p7_c), custom.cards[2].label, 176, 90, 4, F24, COLOR_TITLE);

    // 三场景卡片（70×90 大卡，纵向铺开感）
    constexpr int CARD_Y = 130;
    constexpr int CARD_W = 65;
    constexpr int CARD_H = 90;
    constexpr int GAP = 10;
    int x0 = (SCREEN_W - (CARD_W * 3 + GAP * 2)) / 2;

    // 背景三张卡
    for (int i = 0; i < 3; ++i) {
        int cx = x0 + i * (CARD_W + GAP);
        k10.canvas->canvasRectangle(cx, CARD_Y, CARD_W, CARD_H,
                                    COLOR_CARD, COLOR_CARD, true);
    }

    // 激活卡片（8s 轮换）
    int active = (g_model.view.nowMs / 8000) % 3;
    int cx = x0 + active * (CARD_W + GAP);
    k10.canvas->canvasRectangle(cx, CARD_Y, CARD_W, 3, COLOR_MORANDI, COLOR_MORANDI, true);
    k10.canvas->canvasRectangle(cx, CARD_Y + CARD_H - 3, CARD_W, 3, COLOR_MORANDI, COLOR_MORANDI, true);
    k10.canvas->canvasRectangle(cx, CARD_Y, 3, CARD_H, COLOR_MORANDI, COLOR_MORANDI, true);
    k10.canvas->canvasRectangle(cx + CARD_W - 3, CARD_Y, 3, CARD_H, COLOR_MORANDI, COLOR_MORANDI, true);
    c.p7_active = active;

    // 激活指示点（卡片顶部上方）
    drawDot(cx + CARD_W / 2, CARD_Y - 8, COLOR_MORANDI, 4);

    // 提示
    dirty |= drawField(c.p7_hint, sizeof(c.p7_hint),
                       custom.hintText, 35, 240, 18, F16, COLOR_BLUE);

    dirty |= draw_footer(PAGE_LANDSCAPE_CUSTOM, "[A] Next  [B] Prev");
    dirty |= draw_shake_transition();
    return dirty;
}

// ===========================================================================
// 特殊页
// ===========================================================================

// ---------- SLP: Face-Down —— 极简中央 slogan（修 240px 屏宽疏漏）----------
static bool render_face_down() {
    PageCache& c = g_cache[PAGE_SLEEP_FACE_DOWN];
    bool dirty = false;
    const auto& face = g_model.faceDown;

    if (!c.ever_drawn) {
        k10.canvas->canvasClear();
        c.ever_drawn = true;
        dirty = true;
    }

    // 莫兰迪绿点
    drawDot(116, 132, COLOR_MORANDI, 5);

    // slogan (F24 短行 < 144px 居中显示)
    dirty |= drawField(c.p8_l1, sizeof(c.p8_l1),
                       face.line1, 80, 162, 6, F24, COLOR_MORANDI);
    dirty |= drawField(c.p8_l2, sizeof(c.p8_l2),
                       face.line2, 56, 198, 8, F24, COLOR_TITLE);
    dirty |= drawField(c.p8_l3, sizeof(c.p8_l3),
                       face.line3, 78, 236, 8, F24, COLOR_DIM);

    // 中央装饰横线
    if (!c.ever_drawn) {
        k10.canvas->canvasRectangle(96, 268, 48, 1, COLOR_MORANDI_D, COLOR_MORANDI_D, true);
    }

    return dirty;
}

// ---------- CFG: WiFi 配网 ----------
static bool render_config_portal() {
    PageCache& c = g_cache[PAGE_CONFIG_PORTAL];
    bool dirty = draw_header(PAGE_CONFIG_PORTAL, "WiFi Setup");
    draw_header_divider();
    const auto& config = g_model.config;

    // 步骤 1
    dirty |= drawField(c.p9_label1, sizeof(c.p9_label1),
                       "1. Connect to", 5, 56, 14, F16, COLOR_LABEL);
    dirty |= drawField(c.p9_ssid, sizeof(c.p9_ssid),
                       config.ssidText, 5, 86, 14, F24, COLOR_MORANDI);

    // 步骤 2
    dirty |= drawField(c.p9_label2, sizeof(c.p9_label2),
                       "2. Open browser", 5, 142, 14, F16, COLOR_LABEL);
    dirty |= drawField(c.p9_url, sizeof(c.p9_url),
                       config.urlText, 5, 172, 12, F24, COLOR_MORANDI);

    // 步骤 3 提示（莫兰迪沙）—— 字段化避免 ever_drawn 漏画
    dirty |= drawField(c.p9_hint2, sizeof(c.p9_hint2),
                       config.stepText, 5, 232, 18, F16, COLOR_SAND);
    dirty |= drawField(c.p9_hint3, sizeof(c.p9_hint3),
                       config.hintText, 5, 252, 18, F16, COLOR_LABEL);

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
    Serial.println("[D][UI] Morandi theme v3 ready (240x320, F24-large)");

    k10.rgb->write(-1, RGB_OFF);
    for (uint8_t i = 0; i < PAGE_COUNT; ++i) g_cache[i] = PageCache();
    g_last_page = PAGE_COUNT;
    g_last_shake_phase = SHAKE_VISUAL_IDLE;
}

void dn_ui_render() {
    using namespace desknest;
    g_model = dn_build_ui_model();
    const UIPage cur = g_model.view.page;

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
