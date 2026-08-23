/*
 * PC Monitor - reusable composite widgets.
 *
 * Thin wrappers around plain LVGL objects (no custom widget classes): they only
 * bundle creation and update of a few objects that always belong together.
 */
#pragma once

#include <stdint.h>

#include "lvgl.h"
#include "pcmon_proto.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Circular gauge: arc + big value + caption + one line of extra info. */
typedef struct {
    lv_obj_t  *root;
    lv_obj_t  *arc;
    lv_obj_t  *value;   /**< "42%"      */
    lv_obj_t  *caption; /**< "CPU"      */
    lv_obj_t  *sub;     /**< "54 C"     */
    lv_color_t color;
} ui_gauge_t;

void ui_gauge_create(ui_gauge_t *gauge, lv_obj_t *parent, int32_t size, lv_color_t color,
                     const char *caption, const lv_font_t *value_font, int32_t arc_width);

/** @brief Update arc + percentage label from a per-mille load value. */
void ui_gauge_set_load(ui_gauge_t *gauge, uint16_t load_pm);

/** @brief Set the small line under the value (temperature, clock, ...). */
void ui_gauge_set_sub(ui_gauge_t *gauge, const char *text, lv_color_t color);

/** Horizontal meter: caption on the left, value on the right, bar underneath. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *caption;
    lv_obj_t *value;
    lv_obj_t *bar;
} ui_meter_t;

void ui_meter_create(ui_meter_t *meter, lv_obj_t *parent, int32_t w, lv_color_t color,
                     const char *caption);

/** @brief Fill the bar to used/total and print "used / total". */
void ui_meter_set(ui_meter_t *meter, uint32_t used_mib, uint32_t total_mib);

/** @brief Fill the bar from a per-mille value and print @p text on the right. */
void ui_meter_set_pm(ui_meter_t *meter, uint16_t value_pm, const char *text);

/** Key/value row used on the detail pages. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *key;
    lv_obj_t *value;
} ui_stat_t;

void ui_stat_create(ui_stat_t *stat, lv_obj_t *parent, int32_t w, const char *key);

/** Grid of thin vertical bars, one per logical CPU core. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *bars[PCMON_MAX_CORES];
    uint8_t   visible;
} ui_corebars_t;

void ui_corebars_create(ui_corebars_t *cores, lv_obj_t *parent, int32_t w, int32_t h,
                        lv_color_t color);

/** @brief Show @p count bars and set their values from @p load_pct. */
void ui_corebars_set(ui_corebars_t *cores, const uint8_t *load_pct, uint8_t count);

#ifdef __cplusplus
}
#endif
