// src/ui_lvgl.cpp
// DeskNest LVGL renderer. This is the single production UI path.

#include "ui.h"
#include "ai_icon_assets.h"
#include "gesture_icon_assets.h"
#include "boot_logo_asset.h"
#include "config.h"
#include "ui_model.h"
#include "desknest_fonts.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>
#include <unihiker_k10.h>

extern SemaphoreHandle_t xLvglMutex;

namespace desknest {

extern UNIHIKER_K10 k10;

namespace {

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 320;
constexpr int HEADER_H = 32;
constexpr int CONTENT_Y = 34;
constexpr int CONTENT_H = 258;
constexpr int HOME_Y = 296;
constexpr int HOME_H = 24;

static constexpr uint32_t C_BG        = 0x0F1419;
static constexpr uint32_t C_CARD      = 0x1A2128;
static constexpr uint32_t C_CARD_HI   = 0x222B34;
static constexpr uint32_t C_LINE      = 0x2A3447;
static constexpr uint32_t C_TEXT      = 0xECE8DC;
static constexpr uint32_t C_LABEL     = 0x8B95A8;
static constexpr uint32_t C_DIM       = 0x5A6478;
static constexpr uint32_t C_BRAND     = 0x9CB89B;
static constexpr uint32_t C_SAND      = 0xD4B896;
static constexpr uint32_t C_BLUE      = 0xA8B5C4;
static constexpr uint32_t C_GPT       = 0x8EA9C4;
static constexpr uint32_t C_MINIMAX   = 0xB49ACF;
static constexpr uint32_t C_ALERT     = 0xC97C7C;
static constexpr uint32_t C_BOOT      = 0xE16811;

static lv_style_t sty_plain;
static lv_style_t sty_card;
static lv_style_t sty_text24;
static lv_style_t sty_text16;
static lv_style_t sty_ascii14;
static lv_style_t sty_label16;
static lv_style_t sty_dim16;
static lv_style_t sty_brand16;
static lv_style_t sty_brand24;
static lv_style_t sty_time32;
static lv_style_t sty_symbol14;
static lv_style_t sty_bar_track;
static lv_style_t sty_pill_on;
static lv_style_t sty_pill_off;
static lv_style_t sty_divider;
static lv_style_t sty_danger;
static bool s_styles_ready = false;

static lv_obj_t* s_scr = nullptr;
static UIPage s_visible_page = PAGE_COUNT;
static bool s_ready = false;
static uint32_t s_last_ui_update_ms = 0;
static uint32_t s_last_lvgl_pump_ms = 0;

struct ChromeObjects {
    lv_obj_t* header = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* page = nullptr;
    lv_obj_t* gesture_lock = nullptr;
    lv_obj_t* status_group = nullptr;
    lv_obj_t* divider = nullptr;
    lv_obj_t* home = nullptr;
    lv_obj_t* pills[5] = {};
};

struct PageObjects {
    lv_obj_t* root = nullptr;
    bool built = false;

    lv_obj_t* labels[24] = {};
    lv_obj_t* bars[16] = {};
    lv_obj_t* rows[8] = {};
    lv_obj_t* objs[24] = {};
};

struct BootOverlayObjects {
    lv_obj_t* root = nullptr;
    lv_obj_t* panel = nullptr;
    lv_obj_t* logo = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* name = nullptr;
    lv_obj_t* subtitle = nullptr;
    lv_obj_t* tagline = nullptr;
    lv_obj_t* progress_track = nullptr;
    lv_obj_t* progress_fill = nullptr;
    lv_obj_t* progress_head = nullptr;
    lv_obj_t* progress_label = nullptr;
};

static ChromeObjects s_chrome;
static PageObjects s_pages[PAGE_COUNT];
static BootOverlayObjects s_boot;
static UiModel s_model;

class LvglLock {
public:
    LvglLock() {
        if (xLvglMutex) {
            xSemaphoreTake(xLvglMutex, portMAX_DELAY);
            locked_ = true;
        }
    }

    ~LvglLock() {
        if (locked_) xSemaphoreGive(xLvglMutex);
    }

private:
    bool locked_ = false;
};

static const lv_font_t* font_cn_body() {
    return &lv_font_16_bold;
}

static const lv_font_t* font_cn_body_bold() {
    return &lv_font_16_bold;
}

static const lv_font_t* font_cn_title() {
    return &lv_font_24_bold;
}

static const lv_font_t* font_cn_title_bold() {
    return &lv_font_24_bold;
}

static const lv_font_t* font_ascii_body() {
    return &lv_font_14_bold;
}

static const lv_font_t* font_ascii_body_bold() {
    return &lv_font_14_bold;
}

static const lv_font_t* font_symbol() {
    return &lv_font_14;
}

static void style_init_once() {
    if (s_styles_ready) return;
    s_styles_ready = true;

    lv_style_init(&sty_plain);
    lv_style_set_bg_opa(&sty_plain, LV_OPA_TRANSP);
    lv_style_set_border_width(&sty_plain, 0);
    lv_style_set_pad_all(&sty_plain, 0);
    lv_style_set_radius(&sty_plain, 0);

    lv_style_init(&sty_card);
    lv_style_set_bg_color(&sty_card, lv_color_hex(C_CARD));
    lv_style_set_bg_opa(&sty_card, LV_OPA_COVER);
    lv_style_set_border_width(&sty_card, 0);
    lv_style_set_radius(&sty_card, 3);
    lv_style_set_pad_hor(&sty_card, 8);
    lv_style_set_pad_ver(&sty_card, 6);

    lv_style_init(&sty_text24);
    lv_style_set_text_color(&sty_text24, lv_color_hex(C_TEXT));
    lv_style_set_text_font(&sty_text24, font_cn_title());

    lv_style_init(&sty_text16);
    lv_style_set_text_color(&sty_text16, lv_color_hex(C_TEXT));
    lv_style_set_text_font(&sty_text16, font_cn_body());

    lv_style_init(&sty_ascii14);
    lv_style_set_text_color(&sty_ascii14, lv_color_hex(C_TEXT));
    lv_style_set_text_font(&sty_ascii14, font_ascii_body());

    lv_style_init(&sty_label16);
    lv_style_set_text_color(&sty_label16, lv_color_hex(C_LABEL));
    lv_style_set_text_font(&sty_label16, font_cn_body());

    lv_style_init(&sty_dim16);
    lv_style_set_text_color(&sty_dim16, lv_color_hex(C_DIM));
    lv_style_set_text_font(&sty_dim16, font_cn_body());

    lv_style_init(&sty_brand16);
    lv_style_set_text_color(&sty_brand16, lv_color_hex(C_BRAND));
    lv_style_set_text_font(&sty_brand16, font_cn_body());

    lv_style_init(&sty_brand24);
    lv_style_set_text_color(&sty_brand24, lv_color_hex(C_BRAND));
    lv_style_set_text_font(&sty_brand24, font_cn_title());

    lv_style_init(&sty_time32);
    lv_style_set_text_color(&sty_time32, lv_color_hex(C_TEXT));
    lv_style_set_text_font(&sty_time32, font_cn_title());

    lv_style_init(&sty_symbol14);
    lv_style_set_text_color(&sty_symbol14, lv_color_hex(C_LABEL));
    lv_style_set_text_font(&sty_symbol14, font_symbol());

    lv_style_init(&sty_bar_track);
    lv_style_set_bg_color(&sty_bar_track, lv_color_hex(C_LINE));
    lv_style_set_bg_opa(&sty_bar_track, LV_OPA_COVER);
    lv_style_set_border_width(&sty_bar_track, 0);
    lv_style_set_radius(&sty_bar_track, 2);

    lv_style_init(&sty_pill_on);
    lv_style_set_bg_color(&sty_pill_on, lv_color_hex(C_BRAND));
    lv_style_set_bg_opa(&sty_pill_on, LV_OPA_COVER);
    lv_style_set_border_width(&sty_pill_on, 0);
    lv_style_set_radius(&sty_pill_on, 2);

    lv_style_init(&sty_pill_off);
    lv_style_set_bg_color(&sty_pill_off, lv_color_hex(C_LINE));
    lv_style_set_bg_opa(&sty_pill_off, LV_OPA_COVER);
    lv_style_set_border_width(&sty_pill_off, 0);
    lv_style_set_radius(&sty_pill_off, 2);

    lv_style_init(&sty_divider);
    lv_style_set_bg_color(&sty_divider, lv_color_hex(C_LINE));
    lv_style_set_bg_opa(&sty_divider, LV_OPA_COVER);
    lv_style_set_border_width(&sty_divider, 0);
    lv_style_set_radius(&sty_divider, 0);

    lv_style_init(&sty_danger);
    lv_style_set_bg_color(&sty_danger, lv_color_hex(C_CARD));
    lv_style_set_bg_opa(&sty_danger, LV_OPA_COVER);
    lv_style_set_border_side(&sty_danger, LV_BORDER_SIDE_LEFT);
    lv_style_set_border_width(&sty_danger, 2);
    lv_style_set_border_color(&sty_danger, lv_color_hex(C_ALERT));
    lv_style_set_radius(&sty_danger, 2);
    lv_style_set_pad_hor(&sty_danger, 8);
    lv_style_set_pad_ver(&sty_danger, 5);
}

static void plain(lv_obj_t* obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &sty_plain, 0);
}

static lv_obj_t* make_label(lv_obj_t* parent, lv_style_t* style, const char* text = "") {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_add_style(label, style, 0);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

static void set_text(lv_obj_t* label, const char* text) {
    if (!label) return;
    const char* next = text ? text : "";
    const char* current = lv_label_get_text(label);
    if (current && strcmp(current, next) == 0) return;
    lv_label_set_text(label, next);
}

static void set_hhmm_text(lv_obj_t* label, const char* text) {
    if (!label) return;
    char hhmm[8] = "--:--";
    if (text && strlen(text) >= 5) {
        memcpy(hhmm, text, 5);
        hhmm[5] = '\0';
    }
    set_text(label, hhmm);
}

static bool parse_iso_local_time(const char* text, struct tm* out) {
    if (!text || !out) return false;
    int y = 0, mon = 0, d = 0, h = 0, m = 0, s = 0;
    if (sscanf(text, "%d-%d-%dT%d:%d:%d", &y, &mon, &d, &h, &m, &s) < 5) return false;
    memset(out, 0, sizeof(*out));
    out->tm_year = y - 1900;
    out->tm_mon = mon - 1;
    out->tm_mday = d;
    out->tm_hour = h;
    out->tm_min = m;
    out->tm_sec = s;
    out->tm_isdst = -1;
    return true;
}

static void format_expire_time_short(char* out, size_t cap, const char* iso_text) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    struct tm tm_exp = {};
    if (!parse_iso_local_time(iso_text, &tm_exp)) {
        snprintf(out, cap, "--");
        return;
    }
    snprintf(out, cap, "%02d:%02d", tm_exp.tm_hour, tm_exp.tm_min);
}

static time_t parse_iso_local_epoch(const char* iso_text) {
    struct tm tm_value = {};
    if (!parse_iso_local_time(iso_text, &tm_value)) return 0;
    return mktime(&tm_value);
}

static void format_remaining_short(char* out, size_t cap, time_t now_epoch, const char* iso_text) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    struct tm tm_exp = {};
    if (!parse_iso_local_time(iso_text, &tm_exp)) {
        snprintf(out, cap, "--");
        return;
    }
    time_t exp_epoch = mktime(&tm_exp);
    if (exp_epoch <= 0 || now_epoch <= 0) {
        snprintf(out, cap, "%02d:%02d", tm_exp.tm_hour, tm_exp.tm_min);
        return;
    }
    long diff = (long)(exp_epoch - now_epoch);
    if (diff <= 0) {
        snprintf(out, cap, "0m");
        return;
    }
    long hours = diff / 3600;
    long mins = (diff % 3600) / 60;
    if (hours > 0) snprintf(out, cap, "%ldh%02ldm", hours, mins);
    else snprintf(out, cap, "%ldm", mins > 0 ? mins : 1);
}

static void format_remaining_hm(char* out, size_t cap, time_t now_epoch, const char* iso_text) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    struct tm tm_exp = {};
    if (!parse_iso_local_time(iso_text, &tm_exp)) {
        snprintf(out, cap, "--");
        return;
    }
    time_t exp_epoch = mktime(&tm_exp);
    if (exp_epoch <= 0 || now_epoch <= 0) {
        snprintf(out, cap, "%02d:%02d", tm_exp.tm_hour, tm_exp.tm_min);
        return;
    }
    long diff = (long)(exp_epoch - now_epoch);
    if (diff <= 0) {
        snprintf(out, cap, "0h0m");
        return;
    }
    long hours = diff / 3600;
    long mins = (diff % 3600) / 60;
    if ((diff % 60) != 0) ++mins;
    if (mins >= 60) {
        mins -= 60;
        ++hours;
    }
    snprintf(out, cap, "%ldh%02ldm", hours, mins);
}

static void format_week_expire(char* out, size_t cap, time_t now_epoch, const char* iso_text) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    struct tm tm_exp = {};
    if (!parse_iso_local_time(iso_text, &tm_exp)) {
        snprintf(out, cap, "--");
        return;
    }
    time_t exp_epoch = mktime(&tm_exp);
    if (exp_epoch <= 0 || now_epoch <= 0) {
        snprintf(out, cap, "%02d:%02d", tm_exp.tm_hour, tm_exp.tm_min);
        return;
    }
    long diff = (long)(exp_epoch - now_epoch);
    if (diff < 0) diff = 0;
    long days = diff / 86400;
    snprintf(out, cap, "%ldd %02d:%02d", days, tm_exp.tm_hour, tm_exp.tm_min);
}

static void format_date_short(char* out, size_t cap, const char* iso_text) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    struct tm tm_exp = {};
    if (!parse_iso_local_time(iso_text, &tm_exp)) {
        snprintf(out, cap, "--");
        return;
    }
    snprintf(out, cap, "%02d-%02d", tm_exp.tm_mon + 1, tm_exp.tm_mday);
}

static uint32_t bar_color(int pct) {
    if (pct > 85) return C_ALERT;
    if (pct > 50) return C_SAND;
    return C_BRAND;
}

static void set_bar(lv_obj_t* fill, int pct, int max_w) {
    if (!fill) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    const int w = (max_w * pct) / 100;
    if (lv_obj_get_width(fill) != w) lv_obj_set_width(fill, w);
    lv_obj_set_style_bg_color(fill, lv_color_hex(bar_color(pct)), 0);
}

static lv_obj_t* make_row(lv_obj_t* parent, int h = 24, int gap = 6) {
    lv_obj_t* row = lv_obj_create(parent);
    plain(row);
    lv_obj_set_width(row, 220);
    lv_obj_set_height(row, h);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(row, gap, 0);
    return row;
}

static lv_obj_t* make_track(lv_obj_t* parent, int w, int h, lv_obj_t** fill_out) {
    lv_obj_t* track = lv_obj_create(parent);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, w, h);
    lv_obj_add_style(track, &sty_bar_track, 0);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* fill = lv_obj_create(track);
    lv_obj_remove_style_all(fill);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_size(fill, 0, h);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(C_BRAND), 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_radius(fill, 2, 0);
    if (fill_out) *fill_out = fill;
    return track;
}

static lv_obj_t* make_segment_track(lv_obj_t* parent,
                                    int w,
                                    int h,
                                    int segments,
                                    int gap,
                                    lv_obj_t** fills_out) {
    lv_obj_t* row = lv_obj_create(parent);
    plain(row);
    lv_obj_set_size(row, w, h);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(row, gap, 0);

    const int seg_w = (w - gap * (segments - 1)) / segments;
    for (int i = 0; i < segments; ++i) {
        lv_obj_t* track = lv_obj_create(row);
        lv_obj_remove_style_all(track);
        lv_obj_set_size(track, seg_w, h);
        lv_obj_add_style(track, &sty_bar_track, 0);
        lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* fill = lv_obj_create(track);
        lv_obj_remove_style_all(fill);
        lv_obj_set_pos(fill, 0, 0);
        lv_obj_set_size(fill, 0, h);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(fill, lv_color_hex(C_BRAND), 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_radius(fill, 2, 0);
        if (fills_out) fills_out[i] = fill;
    }
    return row;
}

static void set_segmented_bar(lv_obj_t** fills, int count, int pct, uint32_t color) {
    if (!fills) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    const int step = 100 / count;
    for (int i = 0; i < count; ++i) {
        lv_obj_t* fill = fills[i];
        if (!fill) continue;
        lv_obj_t* track = lv_obj_get_parent(fill);
        const int max_w = lv_obj_get_width(track);
        int local = pct - i * step;
        if (local < 0) local = 0;
        if (local > step) local = step;
        const int w = (max_w * local) / step;
        if (lv_obj_get_width(fill) != w) lv_obj_set_width(fill, w);
        lv_obj_set_style_bg_color(fill, lv_color_hex(color), 0);
    }
}

static PageObjects& page_objects(UIPage page) {
    return s_pages[(int)page];
}

static lv_obj_t* make_page_root(UIPage page) {
    PageObjects& po = page_objects(page);
    if (po.root) return po.root;

    po.root = lv_obj_create(s_scr);
    plain(po.root);
    lv_obj_set_size(po.root, SCREEN_W, CONTENT_H);
    lv_obj_set_pos(po.root, 0, CONTENT_Y);
    lv_obj_set_flex_flow(po.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(po.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(po.root, 10, 0);
    lv_obj_set_style_pad_right(po.root, 10, 0);
    lv_obj_set_style_pad_top(po.root, 4, 0);
    lv_obj_set_style_pad_gap(po.root, 6, 0);
    lv_obj_add_flag(po.root, LV_OBJ_FLAG_HIDDEN);
    return po.root;
}

static void chrome_build() {
    if (s_chrome.header) return;

    s_chrome.header = lv_obj_create(s_scr);
    plain(s_chrome.header);
    lv_obj_set_size(s_chrome.header, SCREEN_W, HEADER_H);
    lv_obj_set_pos(s_chrome.header, 0, 0);
    lv_obj_set_flex_flow(s_chrome.header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_chrome.header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(s_chrome.header, 8, 0);

    s_chrome.title = make_label(s_chrome.header, &sty_ascii14, "DeskNest");
    // The countdown owns this left region. The 80px status group is fixed
    // on the right, so neither object can paint across the other.
    lv_obj_set_width(s_chrome.title, 132);
    s_chrome.status_group = lv_obj_create(s_chrome.header);
    plain(s_chrome.status_group);
    lv_obj_set_size(s_chrome.status_group, 80, HEADER_H);
    lv_obj_set_flex_flow(s_chrome.status_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_chrome.status_group, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(s_chrome.status_group, 2, 0);
    s_chrome.gesture_lock = lv_img_create(s_chrome.status_group);
    lv_obj_set_size(s_chrome.gesture_lock, 16, 16);
    lv_img_set_src(s_chrome.gesture_lock, &dn_img_lock_16);
    s_chrome.page = make_label(s_chrome.status_group, &sty_symbol14, "");
    lv_obj_set_width(s_chrome.page, 60);
    lv_obj_set_style_text_align(s_chrome.page, LV_TEXT_ALIGN_CENTER, 0);

    s_chrome.divider = lv_obj_create(s_scr);
    lv_obj_set_size(s_chrome.divider, SCREEN_W - 10, 1);
    lv_obj_set_pos(s_chrome.divider, 5, HEADER_H - 2);
    lv_obj_add_style(s_chrome.divider, &sty_divider, 0);

    s_chrome.home = lv_obj_create(s_scr);
    plain(s_chrome.home);
    lv_obj_set_size(s_chrome.home, SCREEN_W, HOME_H);
    lv_obj_set_pos(s_chrome.home, 0, HOME_Y);
    lv_obj_set_flex_flow(s_chrome.home, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_chrome.home, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(s_chrome.home, 10, 0);

    const char* icons[5] = {
        LV_SYMBOL_HOME,
        LV_SYMBOL_CHARGE,
        LV_SYMBOL_LIST,
        LV_SYMBOL_SETTINGS,
        LV_SYMBOL_SETTINGS,
    };
    for (int i = 0; i < 5; ++i) {
        s_chrome.pills[i] = make_label(s_chrome.home, &sty_symbol14, icons[i]);
        lv_obj_set_width(s_chrome.pills[i], 22);
        lv_obj_set_style_text_align(s_chrome.pills[i], LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void chrome_update(const UiModel& m) {
    const bool special = m.boot.active ||
                         m.view.page == PAGE_SLEEP_FACE_DOWN ||
                         m.view.page == PAGE_CONFIG_PORTAL ||
                         m.view.page == PAGE_BOOT_FAILURE;
    if (special) {
        lv_obj_add_flag(s_chrome.header, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_chrome.divider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_chrome.home, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(s_chrome.header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_chrome.divider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_chrome.home, LV_OBJ_FLAG_HIDDEN);

    char title_buf[32] = "DeskNest";
    if (m.view.page == PAGE_PORTRAIT_OVERVIEW) {
        snprintf(title_buf, sizeof(title_buf), "%s",
                 m.overview.timeText[0] ? m.overview.timeText : "--:--");
    }
    if (m.view.page == PAGE_PORTRAIT_AI_USAGE) {
        if (m.aiUsage.nextRefreshInSec > 0) {
            const unsigned sec = (unsigned)m.aiUsage.nextRefreshInSec;
            if (sec < 60) snprintf(title_buf, sizeof(title_buf), "Refresh %us", sec);
            else snprintf(title_buf, sizeof(title_buf), "Refresh %um%02us", sec / 60, sec % 60);
        } else {
            snprintf(title_buf, sizeof(title_buf), "Refresh --");
        }
    }
    if (m.view.page == PAGE_PORTRAIT_MENU) {
        snprintf(title_buf, sizeof(title_buf), "A pick  B save");
    }
    if (m.view.page == PAGE_PORTRAIT_SETTINGS) {
        snprintf(title_buf, sizeof(title_buf), "A select  B switch");
    }
    set_text(s_chrome.title, title_buf);

    char wifi_buf[24];
    const char* wifi_icon = LV_SYMBOL_WIFI;
    switch (m.status.wifiState) {
        case UI_WIFI_CONNECTED:
            snprintf(wifi_buf, sizeof(wifi_buf), "%s OK", wifi_icon);
            lv_obj_set_style_text_color(s_chrome.page, lv_color_hex(C_BRAND), 0);
            break;
        case UI_WIFI_CONNECTING:
            snprintf(wifi_buf, sizeof(wifi_buf), "%s ...", wifi_icon);
            lv_obj_set_style_text_color(s_chrome.page, lv_color_hex(C_SAND), 0);
            break;
        case UI_WIFI_NO_SSID:
            snprintf(wifi_buf, sizeof(wifi_buf), "%s AP?", wifi_icon);
            lv_obj_set_style_text_color(s_chrome.page, lv_color_hex(C_ALERT), 0);
            break;
        case UI_WIFI_AUTH_FAILED:
            snprintf(wifi_buf, sizeof(wifi_buf), "%s Key", wifi_icon);
            lv_obj_set_style_text_color(s_chrome.page, lv_color_hex(C_ALERT), 0);
            break;
        case UI_WIFI_DISCONNECTED:
            snprintf(wifi_buf, sizeof(wifi_buf), "%s --", wifi_icon);
            lv_obj_set_style_text_color(s_chrome.page, lv_color_hex(C_DIM), 0);
            break;
        case UI_WIFI_UNCONFIGURED:
        default:
            snprintf(wifi_buf, sizeof(wifi_buf), "%s cfg", wifi_icon);
            lv_obj_set_style_text_color(s_chrome.page, lv_color_hex(C_DIM), 0);
            break;
    }
    set_text(s_chrome.page, wifi_buf);

    if (m.header.gestureConfirmEnabled) {
        lv_img_set_src(s_chrome.gesture_lock, &dn_img_lock_16);
        lv_obj_set_style_img_recolor(s_chrome.gesture_lock, lv_color_hex(C_SAND), 0);
        lv_obj_set_style_img_recolor_opa(s_chrome.gesture_lock, LV_OPA_COVER, 0);
    } else {
        lv_img_set_src(s_chrome.gesture_lock, &dn_img_lock_16);
        lv_obj_set_style_img_recolor(s_chrome.gesture_lock, lv_color_hex(C_DIM), 0);
        lv_obj_set_style_img_recolor_opa(s_chrome.gesture_lock, LV_OPA_COVER, 0);
    }

    int active = dn_page_index_in_group(m.view.page);
    int count = dn_page_count_in_group(m.view.page);
    if (count > 5) count = 5;

    for (int i = 0; i < 5; ++i) {
        if (i >= count) {
            lv_obj_add_flag(s_chrome.pills[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_chrome.pills[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_chrome.pills[i],
                                    lv_color_hex(active == i ? C_BRAND : C_DIM),
                                    0);
    }
}

static void boot_overlay_build() {
    if (s_boot.root) return;

    s_boot.root = lv_obj_create(s_scr);
    plain(s_boot.root);
    lv_obj_set_size(s_boot.root, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_boot.root, 0, 0);
    lv_obj_set_style_bg_color(s_boot.root, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_boot.root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_boot.root, LV_OBJ_FLAG_SCROLLABLE);

    s_boot.panel = lv_obj_create(s_boot.root);
    plain(s_boot.panel);
    lv_obj_set_size(s_boot.panel, 220, 170);
    lv_obj_set_pos(s_boot.panel, 10, 58);
    lv_obj_add_style(s_boot.panel, &sty_card, 0);
    lv_obj_set_style_pad_all(s_boot.panel, 12, 0);

    s_boot.logo = lv_img_create(s_boot.panel);
    lv_img_set_src(s_boot.logo, &dn_img_dfrobot_40);
    lv_obj_set_pos(s_boot.logo, 12, 16);
    lv_obj_set_style_img_recolor(s_boot.logo, lv_color_hex(C_BOOT), 0);
    lv_obj_set_style_img_recolor_opa(s_boot.logo, LV_OPA_COVER, 0);

    s_boot.title = make_label(s_boot.panel, &sty_text24, "栖屏");
    lv_obj_set_pos(s_boot.title, 64, 16);
    lv_obj_set_width(s_boot.title, 132);
    lv_obj_set_style_text_align(s_boot.title, LV_TEXT_ALIGN_LEFT, 0);

    s_boot.name = make_label(s_boot.panel, &sty_text24, "DeskNest");
    lv_obj_set_pos(s_boot.name, 64, 42);
    lv_obj_set_width(s_boot.name, 132);

    s_boot.subtitle = make_label(s_boot.panel, &sty_text16, "栖于桌面");
    lv_obj_set_pos(s_boot.subtitle, 64, 78);
    lv_obj_set_width(s_boot.subtitle, 132);

    s_boot.tagline = make_label(s_boot.panel, &sty_text16, "息于常亮之间");
    lv_obj_set_pos(s_boot.tagline, 12, 122);
    lv_obj_set_width(s_boot.tagline, 196);
    lv_obj_set_style_text_color(s_boot.tagline, lv_color_hex(C_LABEL), 0);
    // Boot copy is deliberately ASCII-only: it remains legible before the
    // complete CJK font path and network-backed model have both settled.
    set_text(s_boot.title, "DESKNEST");
    set_text(s_boot.name, "K10");
    set_text(s_boot.subtitle, "Desk companion");
    set_text(s_boot.tagline, "Getting ready");

    s_boot.progress_track = lv_obj_create(s_boot.root);
    lv_obj_remove_style_all(s_boot.progress_track);
    lv_obj_set_size(s_boot.progress_track, 196, 8);
    lv_obj_set_pos(s_boot.progress_track, 22, 264);
    lv_obj_add_style(s_boot.progress_track, &sty_bar_track, 0);

    s_boot.progress_fill = lv_obj_create(s_boot.progress_track);
    lv_obj_remove_style_all(s_boot.progress_fill);
    lv_obj_set_pos(s_boot.progress_fill, 0, 0);
    lv_obj_set_size(s_boot.progress_fill, 0, 8);
    lv_obj_set_style_bg_opa(s_boot.progress_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_boot.progress_fill, lv_color_hex(C_BOOT), 0);
    lv_obj_set_style_border_width(s_boot.progress_fill, 0, 0);
    lv_obj_set_style_radius(s_boot.progress_fill, 2, 0);

    s_boot.progress_head = lv_obj_create(s_boot.root);
    // This marker must be an opaque solid object. Reusing the card style here
    // left the underlying progress rail visible through its padded region.
    plain(s_boot.progress_head);
    lv_obj_set_size(s_boot.progress_head, 38, 20);
    lv_obj_set_style_radius(s_boot.progress_head, 10, 0);
    lv_obj_set_style_bg_color(s_boot.progress_head, lv_color_hex(C_BOOT), 0);
    lv_obj_set_style_bg_opa(s_boot.progress_head, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_boot.progress_head, 0, 0);
    lv_obj_set_style_pad_all(s_boot.progress_head, 0, 0);
    lv_obj_set_pos(s_boot.progress_head, 10, 258);

    s_boot.progress_label = make_label(s_boot.progress_head, &sty_ascii14, "K10");
    lv_obj_center(s_boot.progress_label);
    lv_obj_set_style_text_color(s_boot.progress_label, lv_color_hex(C_BG), 0);
    lv_obj_move_foreground(s_boot.progress_head);

    lv_obj_add_flag(s_boot.root, LV_OBJ_FLAG_HIDDEN);
}

static void set_boot_overlay_opa(uint8_t opa) {
    lv_obj_set_style_bg_opa(s_boot.root, opa, 0);
    lv_obj_set_style_img_opa(s_boot.logo, opa, 0);
    lv_obj_set_style_text_opa(s_boot.title, opa, 0);
    lv_obj_set_style_text_opa(s_boot.name, opa, 0);
    lv_obj_set_style_text_opa(s_boot.subtitle, opa, 0);
    lv_obj_set_style_text_opa(s_boot.tagline, opa, 0);
    lv_obj_set_style_bg_opa(s_boot.progress_track, opa, 0);
    lv_obj_set_style_bg_opa(s_boot.progress_fill, opa, 0);
    lv_obj_set_style_bg_opa(s_boot.progress_head, opa, 0);
    lv_obj_set_style_text_opa(s_boot.progress_label, opa, 0);
}

static void boot_overlay_update(const UiModel& m) {
    boot_overlay_build();

    if (!m.boot.active) {
        lv_obj_add_flag(s_boot.root, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(s_boot.root, LV_OBJ_FLAG_HIDDEN);
    const uint8_t opa = (uint8_t)(255 - ((uint16_t)m.boot.fadePct * 255U) / 100U);
    set_boot_overlay_opa(opa);
    const int track_w = lv_obj_get_width(s_boot.progress_track);
    const int head_w = lv_obj_get_width(s_boot.progress_head);
    int pct = m.boot.progressPct;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_obj_set_width(s_boot.progress_fill, (track_w * pct) / 100);
    const int min_x = 10;
    const int max_x = 10 + track_w - head_w + 12;
    int head_x = min_x + ((max_x - min_x) * pct) / 100;
    if (head_x < min_x) head_x = min_x;
    if (head_x > max_x) head_x = max_x;
    lv_obj_set_pos(s_boot.progress_head, head_x, 258);
}

static void hide_all_pages() {
    for (int i = 0; i < PAGE_COUNT; ++i) {
        if (s_pages[i].root) lv_obj_add_flag(s_pages[i].root, LV_OBJ_FLAG_HIDDEN);
    }
    s_visible_page = PAGE_COUNT;
}

static void build_overview() {
    PageObjects& po = page_objects(PAGE_PORTRAIT_OVERVIEW);
    if (po.built) return;
    lv_obj_t* root = make_page_root(PAGE_PORTRAIT_OVERVIEW);

    // 3:2 home composition: AI usage is primary, environment is secondary.
    // Keep both cards inside the 258px content area: 151 + 5 + 97 = 253px.
    lv_obj_t* ai = lv_obj_create(root);
    lv_obj_set_size(ai, 220, 151);
    lv_obj_add_style(ai, &sty_card, 0);
    lv_obj_set_style_pad_all(ai, 8, 0);
    lv_obj_set_flex_flow(ai, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ai, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(ai, 3, 0);

    lv_obj_t* ai_head = lv_obj_create(ai);
    plain(ai_head);
    lv_obj_set_size(ai_head, 204, 18);
    lv_obj_set_flex_flow(ai_head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ai_head, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    po.labels[1] = make_label(ai_head, &sty_brand16, "AI USAGE");
    po.labels[2] = make_label(ai_head, &sty_dim16, "cached");
    lv_obj_set_width(po.labels[2], 112);
    lv_obj_set_style_text_align(po.labels[2], LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* ai_total = lv_obj_create(ai);
    plain(ai_total);
    lv_obj_set_size(ai_total, 204, 30);
    lv_obj_set_flex_flow(ai_total, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ai_total, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(ai_total, 5, 0);
    po.labels[3] = make_label(ai_total, &sty_brand24, "0%");
    lv_obj_set_width(po.labels[3], 62);
    po.labels[4] = make_label(ai_total, &sty_dim16, "used");
    lv_obj_set_width(po.labels[4], 50);
    make_track(ai, 204, 8, &po.bars[0]);

    const char* provider_names[3] = {"Codex", "ChatGPT", "MiniMax"};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* row = lv_obj_create(ai);
        plain(row);
        lv_obj_set_size(row, 204, 18);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(row, 4, 0);
        po.labels[5 + i] = make_label(row, &sty_label16, provider_names[i]);
        lv_obj_set_width(po.labels[5 + i], 70);
        make_track(row, 88, 6, &po.bars[1 + i]);
        po.labels[8 + i] = make_label(row, &sty_ascii14, "0%");
        lv_obj_set_width(po.labels[8 + i], 34);
        lv_obj_set_style_text_align(po.labels[8 + i], LV_TEXT_ALIGN_RIGHT, 0);
    }

    lv_obj_t* environment = lv_obj_create(root);
    lv_obj_set_size(environment, 220, 97);
    lv_obj_add_style(environment, &sty_card, 0);
    lv_obj_set_style_pad_all(environment, 8, 0);
    lv_obj_set_flex_flow(environment, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(environment, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(environment, 3, 0);

    lv_obj_t* environment_head = lv_obj_create(environment);
    plain(environment_head);
    lv_obj_set_size(environment_head, 204, 18);
    lv_obj_set_flex_flow(environment_head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(environment_head, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    po.labels[11] = make_label(environment_head, &sty_brand16, "ENVIRONMENT");
    po.labels[12] = make_label(environment_head, &sty_dim16, "--");
    lv_obj_set_width(po.labels[12], 100);
    lv_obj_set_style_text_align(po.labels[12], LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* metrics = lv_obj_create(environment);
    plain(metrics);
    lv_obj_set_size(metrics, 204, 26);
    lv_obj_set_flex_flow(metrics, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(metrics, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(metrics, 4, 0);
    for (int i = 0; i < 3; ++i) {
        po.labels[13 + i] = make_label(metrics, &sty_text16, "--");
        lv_obj_set_width(po.labels[13 + i], 65);
    }
    po.labels[16] = make_label(environment, &sty_dim16, "sensor unavailable");
    lv_obj_set_width(po.labels[16], 204);

    po.built = true;
}

static void update_overview(const UiModel& m) {
    PageObjects& po = page_objects(PAGE_PORTRAIT_OVERVIEW);
    char buf[48];

    set_text(po.labels[1], "AI USAGE");
    const char* ai_state = m.aiUsage.warningText && m.aiUsage.warningText[0]
        ? m.aiUsage.warningText
        : (m.aiUsage.updatedAtText && m.aiUsage.updatedAtText[0]
            ? m.aiUsage.updatedAtText : "cached");
    set_text(po.labels[2], ai_state);

    snprintf(buf, sizeof(buf), "%u%%", (unsigned)m.aiUsage.totalPercent);
    set_text(po.labels[3], buf);
    set_text(po.labels[4], "used");
    set_bar(po.bars[0], m.aiUsage.totalPercent, 204);

    const UiUsageItemProps* providers[3] = {
        &m.aiUsage.codex,
        &m.aiUsage.chatgpt,
        &m.aiUsage.minimax,
    };
    const char* fallback_names[3] = {"Codex", "ChatGPT", "MiniMax"};
    for (int i = 0; i < 3; ++i) {
        const UiUsageItemProps& item = *providers[i];
        set_text(po.labels[5 + i], item.name && item.name[0] ? item.name : fallback_names[i]);
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)item.percent);
        set_text(po.labels[8 + i], buf);
        set_bar(po.bars[1 + i], item.percent, 88);
    }

    set_text(po.labels[11], "ENVIRONMENT");
    const char* grade = (m.environment.valid && m.environment.gradeText && m.environment.gradeText[0])
        ? m.environment.gradeText : "sensor --";
    set_text(po.labels[12], grade);

    if (m.environment.valid) {
        snprintf(buf, sizeof(buf), "%.1f°C", (double)m.environment.temperatureC);
        set_text(po.labels[13], buf);
        snprintf(buf, sizeof(buf), "%.0f%%", (double)m.environment.humidityPct);
        set_text(po.labels[14], buf);
        snprintf(buf, sizeof(buf), "%ulx", (unsigned)m.environment.lux);
        set_text(po.labels[15], buf);
        set_text(po.labels[16],
                 m.environment.adviceText && m.environment.adviceText[0]
                     ? m.environment.adviceText : "environment stable");
    } else {
        set_text(po.labels[13], "--");
        set_text(po.labels[14], "--");
        set_text(po.labels[15], "--");
        set_text(po.labels[16], "sensor unavailable");
    }
}

static void build_ai_usage() {
    PageObjects& po = page_objects(PAGE_PORTRAIT_AI_USAGE);
    if (po.built) return;
    lv_obj_t* root = make_page_root(PAGE_PORTRAIT_AI_USAGE);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* top = make_row(root, 46, 4);
    po.labels[0] = make_label(top, &sty_time32, "--:--");
    lv_obj_set_width(po.labels[0], 74);
    lv_obj_set_style_text_align(po.labels[0], LV_TEXT_ALIGN_LEFT, 0);

    lv_obj_t* providers = lv_obj_create(top);
    plain(providers);
    lv_obj_set_size(providers, 142, 46);
    lv_obj_set_flex_flow(providers, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(providers, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(providers, 6, 0);

    lv_obj_t* gpt = lv_obj_create(providers);
    plain(gpt);
    lv_obj_set_size(gpt, 142, 20);
    lv_obj_set_flex_flow(gpt, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gpt, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(gpt, 4, 0);
    po.objs[0] = lv_img_create(gpt);
    lv_img_set_src(po.objs[0], &dn_img_codex_16);
    lv_obj_set_size(po.objs[0], 16, 16);
    lv_obj_set_style_img_recolor(po.objs[0], lv_color_hex(C_GPT), 0);
    lv_obj_set_style_img_recolor_opa(po.objs[0], LV_OPA_COVER, 0);
    make_segment_track(gpt, 84, 12, 4, 2, &po.bars[0]);
    po.labels[4] = make_label(gpt, &sty_ascii14, "0%");
    lv_obj_set_width(po.labels[4], 34);
    lv_obj_set_style_text_align(po.labels[4], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(po.labels[4], lv_color_hex(C_GPT), 0);

    lv_obj_t* minimax = lv_obj_create(providers);
    plain(minimax);
    lv_obj_set_size(minimax, 142, 20);
    lv_obj_set_flex_flow(minimax, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(minimax, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(minimax, 4, 0);
    po.objs[1] = lv_img_create(minimax);
    lv_img_set_src(po.objs[1], &dn_img_minimax_16);
    lv_obj_set_size(po.objs[1], 16, 16);
    lv_obj_set_style_img_recolor(po.objs[1], lv_color_hex(C_MINIMAX), 0);
    lv_obj_set_style_img_recolor_opa(po.objs[1], LV_OPA_COVER, 0);
    make_segment_track(minimax, 84, 12, 4, 2, &po.bars[4]);
    po.labels[5] = make_label(minimax, &sty_ascii14, "0%");
    lv_obj_set_width(po.labels[5], 34);
    lv_obj_set_style_text_align(po.labels[5], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(po.labels[5], lv_color_hex(C_MINIMAX), 0);

    for (int i = 0; i < 4; ++i) {
        lv_obj_t* row = make_row(root, 18, 8);
        po.labels[6 + i * 2] = make_label(row, &sty_text16);
        lv_obj_set_flex_grow(po.labels[6 + i * 2], 1);
        po.labels[7 + i * 2] = make_label(row, &sty_ascii14);
        lv_obj_set_style_text_color(po.labels[7 + i * 2], lv_color_hex(C_BRAND), 0);
        lv_obj_set_width(po.labels[7 + i * 2], 64);
        lv_obj_set_style_text_align(po.labels[7 + i * 2], LV_TEXT_ALIGN_RIGHT, 0);
        make_track(root, 220, 7, &po.bars[8 + i]);
    }

    lv_obj_t* codex_row = make_row(root, 34, 6);
    po.objs[2] = codex_row;
    lv_obj_t* count_box = lv_obj_create(codex_row);
    lv_obj_set_size(count_box, 30, 30);
    lv_obj_add_style(count_box, &sty_card, 0);
    lv_obj_set_style_radius(count_box, 6, 0);
    po.objs[3] = count_box;
    po.labels[14] = make_label(count_box, &sty_ascii14, "0");
    lv_obj_center(po.labels[14]);

    lv_obj_t* bubbles = lv_obj_create(codex_row);
    plain(bubbles);
    lv_obj_set_size(bubbles, 184, 34);
    lv_obj_set_flex_flow(bubbles, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bubbles, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(bubbles, 4, 0);
    po.objs[4] = bubbles;
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* bubble = lv_obj_create(bubbles);
        lv_obj_add_style(bubble, &sty_card, 0);
        lv_obj_set_width(bubble, 85);
        lv_obj_set_style_radius(bubble, 12, 0);
        lv_obj_set_height(bubble, 24);
        lv_obj_set_style_pad_hor(bubble, 0, 0);
        lv_obj_set_style_pad_ver(bubble, 4, 0);
        po.objs[5 + i] = bubble;
        po.labels[15 + i] = make_label(bubble, &sty_ascii14, "");
        lv_obj_center(po.labels[15 + i]);
    }

    po.built = true;
}

static void update_ai_usage(const UiModel& m) {
    PageObjects& po = page_objects(PAGE_PORTRAIT_AI_USAGE);
    char buf[48];
    char codex_buf[160];
    time_t now_epoch = parse_iso_local_epoch(m.aiUsage.serverNow);
    if (now_epoch <= 0) now_epoch = dn_ai_usage_now_epoch();
    const uint8_t chatgpt_left = (uint8_t)(m.aiUsage.chatgpt.percent >= 100 ? 0 : 100 - m.aiUsage.chatgpt.percent);
    const uint8_t minimax_left = (uint8_t)(m.aiUsage.minimax.percent >= 100 ? 0 : 100 - m.aiUsage.minimax.percent);

    set_hhmm_text(po.labels[0], m.overview.timeText);

    snprintf(buf, sizeof(buf), "%u%%", (unsigned)chatgpt_left);
    set_text(po.labels[4], buf);
    set_segmented_bar(&po.bars[0], 4, chatgpt_left, C_GPT);

    snprintf(buf, sizeof(buf), "%u%%", (unsigned)minimax_left);
    set_text(po.labels[5], buf);
    set_segmented_bar(&po.bars[4], 4, minimax_left, C_MINIMAX);

    struct UsageWindowRow {
        const char* name;
        uint8_t percent;
        uint32_t color;
    };
    const UsageWindowRow rows[4] = {
        {"ChatGPT 5h", m.aiUsage.chatgpt.percent, C_GPT},
        {"ChatGPT Week", m.aiUsage.chatgpt.weeklyPercent, C_GPT},
        {"MiniMax 5h", m.aiUsage.minimax.percent, C_MINIMAX},
        {"MiniMax Week", m.aiUsage.minimax.weeklyPercent, C_MINIMAX},
    };
    const char* expire_ats[4] = {
        m.aiUsage.chatgpt.fiveHourExpireAt,
        m.aiUsage.chatgpt.weekExpireAt,
        m.aiUsage.minimax.fiveHourExpireAt,
        m.aiUsage.minimax.weekExpireAt,
    };
    for (int i = 0; i < 4; ++i) {
        set_text(po.labels[6 + i * 2], rows[i].name);
        if ((i % 2) == 0) format_remaining_hm(buf, sizeof(buf), now_epoch, expire_ats[i]);
        else format_week_expire(buf, sizeof(buf), now_epoch, expire_ats[i]);
        set_text(po.labels[7 + i * 2], buf);
        lv_obj_set_style_text_color(po.labels[7 + i * 2], lv_color_hex(rows[i].color), 0);
        set_bar(po.bars[8 + i], rows[i].percent, 220);
        lv_obj_set_style_bg_color(po.bars[8 + i], lv_color_hex(rows[i].color), 0);
    }

    snprintf(codex_buf, sizeof(codex_buf), "%u", (unsigned)m.aiUsage.codexResetCount);
    set_text(po.labels[14], codex_buf);
    for (uint8_t i = 0; i < 4; ++i) {
        if (!po.objs[5 + i] || !po.labels[15 + i]) continue;
        if (i < m.aiUsage.codexResetCount) {
            char date_text[24];
            format_date_short(date_text, sizeof(date_text), m.aiUsage.codexResets[i].expireAt);
            snprintf(codex_buf, sizeof(codex_buf), "CR%u %s", (unsigned)(i + 1), date_text);
            set_text(po.labels[15 + i], codex_buf);
            lv_obj_clear_flag(po.objs[5 + i], LV_OBJ_FLAG_HIDDEN);
        } else {
            set_text(po.labels[15 + i], "");
            lv_obj_add_flag(po.objs[5 + i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (po.objs[2]) {
        if (m.aiUsage.codexResetCount > 0) lv_obj_clear_flag(po.objs[2], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(po.objs[2], LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_menu() {
    PageObjects& po = page_objects(PAGE_PORTRAIT_MENU);
    if (po.built) return;
    lv_obj_t* root = make_page_root(PAGE_PORTRAIT_MENU);
    lv_obj_set_style_pad_gap(root, 5, 0);

    lv_obj_t* prompt = lv_obj_create(root);
    lv_obj_set_size(prompt, 220, 56);
    lv_obj_add_style(prompt, &sty_card, 0);
    lv_obj_set_style_pad_all(prompt, 8, 0);
    po.labels[0] = make_label(prompt, &sty_brand16);
    lv_obj_set_pos(po.labels[0], 0, 0);
    lv_obj_set_width(po.labels[0], 200);
    po.labels[1] = make_label(prompt, &sty_dim16);
    lv_obj_set_pos(po.labels[1], 0, 26);
    lv_obj_set_width(po.labels[1], 200);

    for (int i = 0; i < 5; ++i) {
        lv_obj_t* row = make_row(root, 24, 6);
        po.rows[i] = row;
        po.labels[2 + i * 4] = make_label(row, &sty_brand16, " ");
        lv_obj_set_width(po.labels[2 + i * 4], 14);
        po.labels[3 + i * 4] = make_label(row, &sty_text16);
        lv_obj_set_flex_grow(po.labels[3 + i * 4], 1);
        po.labels[4 + i * 4] = make_label(row, &sty_ascii14);
        lv_obj_set_width(po.labels[4 + i * 4], 32);
        lv_obj_set_style_text_align(po.labels[4 + i * 4], LV_TEXT_ALIGN_RIGHT, 0);
        po.labels[5 + i * 4] = make_label(row, &sty_ascii14);
        lv_obj_set_width(po.labels[5 + i * 4], 38);
        lv_obj_set_style_text_align(po.labels[5 + i * 4], LV_TEXT_ALIGN_RIGHT, 0);
    }

    po.labels[22] = make_label(root, &sty_dim16, "A pick  B save");
    lv_obj_set_width(po.labels[22], 220);
    lv_obj_set_style_text_align(po.labels[22], LV_TEXT_ALIGN_CENTER, 0);
    po.built = true;
}

static void update_menu(const UiModel& m) {
    PageObjects& po = page_objects(PAGE_PORTRAIT_MENU);
    set_text(po.labels[0], m.menu.ask);
    set_text(po.labels[1], m.menu.lastMeal);

    int row = 0;
    for (uint8_t g = 0; g < m.menu.groupCount && g < 4 && row < 5; ++g) {
        const UiMenuGroupProps& grp = m.menu.groups[g];
        for (uint8_t c = 0; c < grp.candidateCount && c < 6 && row < 5; ++c) {
            const UiMenuCandidateProps& item = grp.candidates[c];
            set_text(po.labels[2 + row * 4], item.active ? ">" : " ");
            set_text(po.labels[3 + row * 4], item.name);
            set_text(po.labels[4 + row * 4], item.price);
            char score[12];
            snprintf(score, sizeof(score), "%u.%u", item.score / 10, item.score % 10);
            set_text(po.labels[5 + row * 4], score);

            lv_obj_clear_flag(po.rows[row], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_opa(po.rows[row], item.active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_color(po.rows[row], lv_color_hex(C_CARD_HI), 0);
            ++row;
        }
    }

    while (row < 5) {
        lv_obj_add_flag(po.rows[row], LV_OBJ_FLAG_HIDDEN);
        ++row;
    }

    set_text(po.labels[22], "A pick  B save");
}

static void build_environment() {
    PageObjects& po = page_objects(PAGE_PORTRAIT_ENVIRONMENT);
    if (po.built) return;
    lv_obj_t* root = make_page_root(PAGE_PORTRAIT_ENVIRONMENT);

    lv_obj_t* score = make_row(root, 34, 8);
    make_label(score, &sty_ascii14, "Comfort");
    po.labels[0] = make_label(score, &sty_brand24);
    po.labels[1] = make_label(score, &sty_dim16);

    make_track(root, 220, 12, &po.bars[0]);

    for (int i = 0; i < 3; ++i) {
        lv_obj_t* row = make_row(root, 30, 8);
        po.labels[2 + i * 2] = make_label(row, &sty_ascii14);
        lv_obj_set_width(po.labels[2 + i * 2], 52);
        po.labels[3 + i * 2] = make_label(row, &sty_text16);
    }

    lv_obj_t* advice = lv_obj_create(root);
    lv_obj_set_size(advice, 220, 40);
    lv_obj_add_style(advice, &sty_card, 0);
    po.labels[8] = make_label(advice, &sty_text16);

    po.built = true;
}

static void update_environment(const UiModel& m) {
    PageObjects& po = page_objects(PAGE_PORTRAIT_ENVIRONMENT);
    char buf[48];

    snprintf(buf, sizeof(buf), "%u", (unsigned)m.environment.score);
    set_text(po.labels[0], buf);
    set_text(po.labels[1], m.environment.gradeText);
    set_bar(po.bars[0], m.environment.score, 220);

    set_text(po.labels[2], "Temp");
    if (m.environment.valid) {
        snprintf(buf, sizeof(buf), "%.1f C | %s",
                 m.environment.temperatureC, m.environment.temperatureGrade);
    } else {
        snprintf(buf, sizeof(buf), "-- | %s", m.environment.temperatureGrade);
    }
    set_text(po.labels[3], buf);

    set_text(po.labels[4], "Hum");
    if (m.environment.valid) {
        snprintf(buf, sizeof(buf), "%.0f%% | %s",
                 m.environment.humidityPct, m.environment.humidityGrade);
    } else {
        snprintf(buf, sizeof(buf), "-- | %s", m.environment.humidityGrade);
    }
    set_text(po.labels[5], buf);

    set_text(po.labels[6], "Light");
    snprintf(buf, sizeof(buf), "%u lx | %s", (unsigned)m.environment.lux, m.environment.lightGrade);
    set_text(po.labels[7], buf);

    set_text(po.labels[8], m.environment.adviceText);
}

static void build_settings() {
    PageObjects& po = page_objects(PAGE_PORTRAIT_SETTINGS);
    if (po.built) return;
    lv_obj_t* root = make_page_root(PAGE_PORTRAIT_SETTINGS);
    for (int i = 0; i < 6; ++i) {
        lv_obj_t* row = make_row(root, 32, 5);
        po.rows[i] = row;
        po.labels[1 + i * 2] = make_label(row, &sty_text16);
        lv_obj_set_flex_grow(po.labels[1 + i * 2], 1);
        po.labels[2 + i * 2] = make_label(row, &sty_text16);
        lv_obj_set_style_text_align(po.labels[2 + i * 2], LV_TEXT_ALIGN_RIGHT, 0);
    }

    po.built = true;
}

static void update_settings(const UiModel& m) {
    PageObjects& po = page_objects(PAGE_PORTRAIT_SETTINGS);
    uint8_t n = m.settings.rowCount;
    if (n > 6) n = 6;

    for (int i = 0; i < 6; ++i) {
        if (i >= n) {
            lv_obj_add_flag(po.rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(po.rows[i], LV_OBJ_FLAG_HIDDEN);
        set_text(po.labels[1 + i * 2], m.settings.rows[i].label);
        set_text(po.labels[2 + i * 2], m.settings.rows[i].value);
        lv_obj_set_style_bg_opa(po.rows[i], i == m.settings.selectedIndex ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(po.rows[i], lv_color_hex(C_CARD_HI), 0);
    }
}

static void build_face_down() {
    PageObjects& po = page_objects(PAGE_SLEEP_FACE_DOWN);
    if (po.built) return;
    lv_obj_t* root = make_page_root(PAGE_SLEEP_FACE_DOWN);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, SCREEN_W, SCREEN_H);
    lv_obj_set_style_pad_top(root, 102, 0);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    po.labels[0] = make_label(root, &sty_brand24, "REST");
    po.labels[1] = make_label(root, &sty_text16);
    po.labels[2] = make_label(root, &sty_dim16);
    po.built = true;
}

static void update_face_down(const UiModel& m) {
    PageObjects& po = page_objects(PAGE_SLEEP_FACE_DOWN);
    set_text(po.labels[0], m.faceDown.line1);
    set_text(po.labels[1], m.faceDown.line2);
    set_text(po.labels[2], m.faceDown.line3);
}

static void build_config() {
    PageObjects& po = page_objects(PAGE_CONFIG_PORTAL);
    if (po.built) return;
    lv_obj_t* root = make_page_root(PAGE_CONFIG_PORTAL);
    lv_obj_set_style_pad_top(root, 42, 0);
    po.labels[0] = make_label(root, &sty_ascii14, "WiFi Setup");
    lv_obj_set_style_text_color(po.labels[0], lv_color_hex(C_BRAND), 0);
    po.labels[1] = make_label(root, &sty_ascii14);
    po.labels[2] = make_label(root, &sty_ascii14);
    po.labels[3] = make_label(root, &sty_ascii14);
    lv_obj_set_style_text_color(po.labels[3], lv_color_hex(C_DIM), 0);
    po.labels[4] = make_label(root, &sty_ascii14);
    lv_obj_set_style_text_color(po.labels[4], lv_color_hex(C_LABEL), 0);
    po.built = true;
}

static void update_config(const UiModel& m) {
    PageObjects& po = page_objects(PAGE_CONFIG_PORTAL);
    char buf[48];
    snprintf(buf, sizeof(buf), "SSID %s", m.config.ssidText);
    set_text(po.labels[1], buf);
    snprintf(buf, sizeof(buf), "URL %s", m.config.urlText);
    set_text(po.labels[2], buf);
    set_text(po.labels[3], m.config.stepText);
    set_text(po.labels[4], m.config.hintText);
}

static void build_boot_failure() {
    PageObjects& po = page_objects(PAGE_BOOT_FAILURE);
    if (po.built) return;
    lv_obj_t* root = make_page_root(PAGE_BOOT_FAILURE);
    lv_obj_set_style_pad_top(root, 44, 0);

    po.labels[0] = make_label(root, &sty_ascii14, "Boot Error");
    lv_obj_set_style_text_color(po.labels[0], lv_color_hex(C_ALERT), 0);
    po.labels[1] = make_label(root, &sty_text16);
    lv_obj_set_width(po.labels[1], 220);
    po.labels[2] = make_label(root, &sty_dim16);
    lv_obj_set_width(po.labels[2], 220);
    po.labels[3] = make_label(root, &sty_ascii14);
    lv_obj_set_width(po.labels[3], 220);
    lv_obj_set_style_text_color(po.labels[3], lv_color_hex(C_LABEL), 0);
    po.built = true;
}

static void update_boot_failure(const UiModel& m) {
    PageObjects& po = page_objects(PAGE_BOOT_FAILURE);
    set_text(po.labels[0], m.bootFailure.title);
    set_text(po.labels[1], m.bootFailure.detail);
    set_text(po.labels[2], "Home page is blocked until init succeeds");
    set_text(po.labels[3], m.bootFailure.hint);
}

static void build_page(UIPage page) {
    switch (page) {
        case PAGE_PORTRAIT_OVERVIEW:    build_overview(); break;
        case PAGE_PORTRAIT_AI_USAGE:    build_ai_usage(); break;
        case PAGE_PORTRAIT_MENU:        build_menu(); break;
        case PAGE_PORTRAIT_ENVIRONMENT: build_environment(); break;
        case PAGE_PORTRAIT_SETTINGS:    build_settings(); break;
        case PAGE_SLEEP_FACE_DOWN:      build_face_down(); break;
        case PAGE_CONFIG_PORTAL:        build_config(); break;
        case PAGE_BOOT_FAILURE:         build_boot_failure(); break;
        default:                        build_overview(); break;
    }
}

static UIPage normalized_page(UIPage page) {
    switch (page) {
        case PAGE_PORTRAIT_OVERVIEW:
        case PAGE_PORTRAIT_AI_USAGE:
        case PAGE_PORTRAIT_MENU:
        case PAGE_PORTRAIT_ENVIRONMENT:
        case PAGE_PORTRAIT_SETTINGS:
        case PAGE_SLEEP_FACE_DOWN:
        case PAGE_CONFIG_PORTAL:
        case PAGE_BOOT_FAILURE:
            return page;
        default:
            return PAGE_PORTRAIT_OVERVIEW;
    }
}

static void show_page(UIPage raw_page) {
    UIPage page = normalized_page(raw_page);
    build_page(page);

    if (s_visible_page != PAGE_COUNT && s_visible_page != page) {
        lv_obj_add_flag(page_objects(s_visible_page).root, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(page_objects(page).root, LV_OBJ_FLAG_HIDDEN);
    s_visible_page = page;
}

static void update_page(const UiModel& m) {
    switch (normalized_page(m.view.page)) {
        case PAGE_PORTRAIT_OVERVIEW:    update_overview(m); break;
        case PAGE_PORTRAIT_AI_USAGE:    update_ai_usage(m); break;
        case PAGE_PORTRAIT_MENU:        update_menu(m); break;
        case PAGE_PORTRAIT_ENVIRONMENT: update_environment(m); break;
        case PAGE_PORTRAIT_SETTINGS:    update_settings(m); break;
        case PAGE_SLEEP_FACE_DOWN:      update_face_down(m); break;
        case PAGE_CONFIG_PORTAL:        update_config(m); break;
        case PAGE_BOOT_FAILURE:         update_boot_failure(m); break;
        default:                        update_overview(m); break;
    }
}

} // namespace

} // namespace desknest

void dn_ui_setup() {
    Serial.println("[D][UI] LVGL setup");

    desknest::k10.initScreen(2);

    desknest::LvglLock lock;
    desknest::s_scr = lv_scr_act();
    desknest::style_init_once();

    lv_obj_set_style_bg_color(desknest::s_scr, lv_color_hex(desknest::C_BG), 0);
    lv_obj_set_style_bg_opa(desknest::s_scr, LV_OPA_COVER, 0);

    desknest::chrome_build();
    desknest::boot_overlay_build();

    desknest::s_model = desknest::dn_build_ui_model();
    desknest::chrome_update(desknest::s_model);
    desknest::boot_overlay_update(desknest::s_model);
    if (!desknest::s_model.boot.active) {
        desknest::show_page(desknest::s_model.view.page);
        desknest::update_page(desknest::s_model);
    } else {
        desknest::hide_all_pages();
    }

    lv_task_handler();
    desknest::s_last_ui_update_ms = millis();
    desknest::s_last_lvgl_pump_ms = desknest::s_last_ui_update_ms;
    desknest::s_ready = true;
    Serial.println("[D][UI] LVGL ready");
}

void dn_ui_render() {
    if (!desknest::s_ready) return;

    desknest::s_model = desknest::dn_build_ui_model();
    const uint32_t now = millis();
    const desknest::UIPage page = desknest::normalized_page(desknest::s_model.view.page);
    const bool page_changed = (page != desknest::s_visible_page);
    const uint32_t ui_interval = desknest::s_model.boot.active ? 40 : 200;
    const bool update_due = (now - desknest::s_last_ui_update_ms) >= ui_interval;
    const bool pump_due = (now - desknest::s_last_lvgl_pump_ms) >= 20;

    if (!page_changed && !update_due && !pump_due) return;

    desknest::LvglLock lock;
    if (page_changed || update_due) {
        desknest::chrome_update(desknest::s_model);
        desknest::boot_overlay_update(desknest::s_model);
        if (desknest::s_model.boot.active) {
            desknest::hide_all_pages();
        } else {
            desknest::show_page(page);
            desknest::update_page(desknest::s_model);
        }
        desknest::s_last_ui_update_ms = now;
    }

    lv_task_handler();
    desknest::s_last_lvgl_pump_ms = now;
}
