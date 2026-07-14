/**
 * @file desknest_fonts.h
 * @brief DeskNest font declarations
 *
 * Font sources:
 * - Montserrat (Medium/Bold): ASCII alphanumeric
 * - NotoSansSC: Chinese characters + punctuation
 * - FontAwesome: LVGL icons/symbols
 */
#ifndef DESKNEST_FONTS_H
#define DESKNEST_FONTS_H

#include <lvgl.h>

// Regular fonts
LV_FONT_DECLARE(lv_font_14);
LV_FONT_DECLARE(lv_font_16);
LV_FONT_DECLARE(lv_font_16_dynamic);
LV_FONT_DECLARE(lv_font_24);

// Bold fonts
LV_FONT_DECLARE(lv_font_14_bold);
LV_FONT_DECLARE(lv_font_16_bold);
LV_FONT_DECLARE(lv_font_24_bold);

#endif // DESKNEST_FONTS_H
