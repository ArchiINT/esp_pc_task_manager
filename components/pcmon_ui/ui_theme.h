/*
 * PC Monitor - colours, fonts and small formatting helpers shared by all pages.
 *
 * The interface is English only on purpose: the built-in LVGL Montserrat fonts
 * ship Latin glyphs only, and embedding a Cyrillic face would cost flash for no
 * functional gain.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Palette ------------------------------------------------------------- */

#define UI_COLOR_BG        lv_color_hex(0x0A0E13) /* screen background        */
#define UI_COLOR_CARD      lv_color_hex(0x141C26) /* panel background         */
#define UI_COLOR_BORDER    lv_color_hex(0x223040) /* panel outline            */
#define UI_COLOR_TRACK     lv_color_hex(0x1E2A36) /* empty part of arcs/bars  */
#define UI_COLOR_TEXT      lv_color_hex(0xE6EDF3)
#define UI_COLOR_TEXT_DIM  lv_color_hex(0x7F8EA0)

#define UI_COLOR_CPU       lv_color_hex(0x22D3EE) /* cyan                     */
#define UI_COLOR_GPU       lv_color_hex(0xA78BFA) /* violet                   */
#define UI_COLOR_RAM       lv_color_hex(0x34D399) /* green                    */
#define UI_COLOR_VRAM      lv_color_hex(0xF472B6) /* pink                     */
#define UI_COLOR_WARN      lv_color_hex(0xFBBF24)
#define UI_COLOR_DANGER    lv_color_hex(0xF43F5E)

/* --- Geometry ------------------------------------------------------------ */

#define UI_HEADER_H        24
#define UI_FOOTER_H        10
#define UI_CONTENT_Y       UI_HEADER_H
#define UI_CONTENT_H       (CONFIG_PCMON_LCD_V_RES - UI_HEADER_H - UI_FOOTER_H)

/*
 * Panels this narrow cannot hold two columns of text side by side, so the pages
 * stack their rows instead and shrink the gauges.  A 135x240 module lands here,
 * a 240x240 one does not.  Usable in #if - both operands are plain integers.
 */
#define UI_NARROW          (CONFIG_PCMON_LCD_H_RES < 200)
#define UI_PAD             (UI_NARROW ? 8 : 16)
#define UI_ROW_W           (CONFIG_PCMON_LCD_H_RES - 2 * UI_PAD)

/* --- Fonts --------------------------------------------------------------- */

#define UI_FONT_TINY       (&lv_font_montserrat_12)
#define UI_FONT_SMALL      (&lv_font_montserrat_14)
#define UI_FONT_MEDIUM     (&lv_font_montserrat_16)
#define UI_FONT_LARGE      (&lv_font_montserrat_20)
#define UI_FONT_HUGE       (&lv_font_montserrat_28)

/* --- Helpers ------------------------------------------------------------- */

/** @brief Green / cyan / amber / red depending on temperature (0.1 degC). */
lv_color_t ui_temp_color(int16_t temp_dc);

/** @brief Accent colour that turns amber and then red under heavy load. */
lv_color_t ui_load_color(lv_color_t base, uint16_t load_pm);

/** @brief Format seconds as "3d 04:21" or "04:21:07". */
void ui_format_uptime(char *buf, size_t size, uint32_t seconds);

/** @brief Format MiB as "12.4G" / "812M" so it fits into narrow labels. */
void ui_format_mib(char *buf, size_t size, uint32_t mib);

/** @brief Container with the standard card look (rounded, bordered, padded). */
lv_obj_t *ui_card_create(lv_obj_t *parent, int32_t w, int32_t h);

/** @brief Plain transparent container without scrolling or padding. */
lv_obj_t *ui_container_create(lv_obj_t *parent, int32_t w, int32_t h);

/** @brief Label with the given font/colour, no wrapping. */
lv_obj_t *ui_label_create(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                          const char *text);

#ifdef __cplusplus
}
#endif
