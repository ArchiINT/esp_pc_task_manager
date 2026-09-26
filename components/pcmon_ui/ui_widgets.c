/*
 * PC Monitor - reusable composite widgets.
 */
#include "ui_widgets.h"

#include <stdio.h>

/* Arc geometry: a 270 degree sweep with the opening at the bottom. */
#define GAUGE_ANGLE_START 135
#define GAUGE_ANGLE_END   45
#define GAUGE_RANGE_PM    1000

void ui_gauge_create(ui_gauge_t *gauge, lv_obj_t *parent, int32_t size, lv_color_t color,
                     const char *caption, const lv_font_t *value_font, int32_t arc_width)
{
    gauge->color = color;
    gauge->root  = ui_container_create(parent, size, size);

    gauge->arc = lv_arc_create(gauge->root);
    lv_obj_set_size(gauge->arc, size, size);
    lv_obj_center(gauge->arc);
    lv_arc_set_rotation(gauge->arc, 0);
    lv_arc_set_bg_angles(gauge->arc, GAUGE_ANGLE_START, GAUGE_ANGLE_END);
    lv_arc_set_range(gauge->arc, 0, GAUGE_RANGE_PM);
    lv_arc_set_value(gauge->arc, 0);

    /* Static read-out: no knob, no input handling. */
    lv_obj_remove_style(gauge->arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(gauge->arc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_arc_width(gauge->arc, arc_width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(gauge->arc, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(gauge->arc, arc_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(gauge->arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(gauge->arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(gauge->arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(gauge->arc, 0, LV_PART_MAIN);

    gauge->caption = ui_label_create(gauge->root, UI_FONT_TINY, UI_COLOR_TEXT_DIM, caption);
    lv_obj_align(gauge->caption, LV_ALIGN_CENTER, 0, -size / 4);

    gauge->value = ui_label_create(gauge->root, value_font, UI_COLOR_TEXT, "--");
    lv_obj_align(gauge->value, LV_ALIGN_CENTER, 0, 0);

    gauge->sub = ui_label_create(gauge->root, UI_FONT_TINY, UI_COLOR_TEXT_DIM, "");
    lv_obj_align(gauge->sub, LV_ALIGN_CENTER, 0, size / 4);
}

void ui_gauge_set_load(ui_gauge_t *gauge, uint16_t load_pm)
{
    if (load_pm > GAUGE_RANGE_PM) {
        load_pm = GAUGE_RANGE_PM;
    }

    lv_arc_set_value(gauge->arc, load_pm);
    lv_obj_set_style_arc_color(gauge->arc, ui_load_color(gauge->color, load_pm), LV_PART_INDICATOR);
    lv_label_set_text_fmt(gauge->value, "%u%%", (unsigned)((load_pm + 5U) / 10U));
}

void ui_gauge_set_sub(ui_gauge_t *gauge, const char *text, lv_color_t color)
{
    lv_label_set_text(gauge->sub, text);
    lv_obj_set_style_text_color(gauge->sub, color, LV_PART_MAIN);
    lv_obj_align(gauge->sub, LV_ALIGN_CENTER, 0, lv_obj_get_height(gauge->root) / 4);
}

/* -------------------------------------------------------------------------- */

/* A wider panel would earn a bigger font here; a taller one only earns air. */
#if UI_TALL
#define METER_FONT  UI_FONT_SMALL
#else
#define METER_FONT  UI_FONT_TINY
#endif

void ui_meter_create(ui_meter_t *meter, lv_obj_t *parent, int32_t w, lv_color_t color,
                     const char *caption)
{
    meter->root = ui_container_create(parent, w, UI_METER_H);

    meter->caption = ui_label_create(meter->root, METER_FONT, UI_COLOR_TEXT_DIM, caption);
    lv_obj_align(meter->caption, LV_ALIGN_TOP_LEFT, 0, 0);

    meter->value = ui_label_create(meter->root, METER_FONT, UI_COLOR_TEXT, "--");
    lv_obj_align(meter->value, LV_ALIGN_TOP_RIGHT, 0, 0);

    meter->bar = lv_bar_create(meter->root);
    lv_obj_set_size(meter->bar, w, UI_METER_BAR_H);
    lv_obj_align(meter->bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_bar_set_range(meter->bar, 0, 1000);
    lv_bar_set_value(meter->bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(meter->bar, UI_METER_BAR_H / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(meter->bar, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_radius(meter->bar, UI_METER_BAR_H / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(meter->bar, color, LV_PART_INDICATOR);
}

void ui_meter_set(ui_meter_t *meter, uint32_t used_mib, uint32_t total_mib)
{
    char used_str[12];
    char total_str[12];
    char text[28];

    ui_format_mib(used_str, sizeof(used_str), used_mib);
    ui_format_mib(total_str, sizeof(total_str), total_mib);
    snprintf(text, sizeof(text), "%s / %s", used_str, total_str);

    const uint16_t pm = (total_mib > 0)
                            ? (uint16_t)(((uint64_t)used_mib * 1000U) / total_mib)
                            : 0U;
    ui_meter_set_pm(meter, pm, text);
}

void ui_meter_set_pm(ui_meter_t *meter, uint16_t value_pm, const char *text)
{
    if (value_pm > 1000U) {
        value_pm = 1000U;
    }
    lv_bar_set_value(meter->bar, value_pm, LV_ANIM_OFF);
    lv_label_set_text(meter->value, text);
}

/* -------------------------------------------------------------------------- */

void ui_stat_create(ui_stat_t *stat, lv_obj_t *parent, int32_t w, const char *key)
{
    stat->root = ui_container_create(parent, w, UI_STAT_H);

    stat->key = ui_label_create(stat->root, UI_FONT_TINY, UI_COLOR_TEXT_DIM, key);
    lv_obj_align(stat->key, LV_ALIGN_LEFT_MID, 0, 0);

    stat->value = ui_label_create(stat->root, UI_FONT_SMALL, UI_COLOR_TEXT, "--");
    lv_obj_align(stat->value, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* -------------------------------------------------------------------------- */

/* 16 bars per row on a 208 px wide page, so 32 threads fill exactly two rows. */
#define COREBAR_W   10
#define COREBAR_GAP 3

void ui_corebars_create(ui_corebars_t *cores, lv_obj_t *parent, int32_t w, int32_t h,
                        lv_color_t color)
{
    cores->root    = ui_container_create(parent, w, h);
    cores->visible = 0;

    lv_obj_set_flex_flow(cores->root, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cores->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(cores->root, COREBAR_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_row(cores->root, COREBAR_GAP, LV_PART_MAIN);

    /* Bars are created once and hidden until the host reports the core count. */
    for (int i = 0; i < PCMON_MAX_CORES; i++) {
        lv_obj_t *bar = lv_bar_create(cores->root);
        lv_obj_set_size(bar, COREBAR_W, h / 2 - COREBAR_GAP);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, UI_COLOR_TRACK, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        cores->bars[i] = bar;
    }
}

void ui_corebars_set(ui_corebars_t *cores, const uint8_t *load_pct, uint8_t count)
{
    if (count > PCMON_MAX_CORES) {
        count = PCMON_MAX_CORES;
    }

    for (uint8_t i = 0; i < PCMON_MAX_CORES; i++) {
        if (i < count) {
            if (i >= cores->visible) {
                lv_obj_remove_flag(cores->bars[i], LV_OBJ_FLAG_HIDDEN);
            }
            lv_bar_set_value(cores->bars[i], load_pct[i], LV_ANIM_OFF);
        } else if (i < cores->visible) {
            lv_obj_add_flag(cores->bars[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    cores->visible = count;
}
