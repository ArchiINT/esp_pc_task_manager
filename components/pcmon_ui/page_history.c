/*
 * PC Monitor - history page: one minute of CPU and GPU load.
 *
 * Telemetry arrives at ~10 Hz, which would fill a 60 point chart in six
 * seconds, so samples are down-converted to 1 Hz here.
 */
#include "ui_page.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define HISTORY_POINTS      60
#define HISTORY_INTERVAL_MS 1000
#if UI_NARROW
/* Both legends on one line need ~150 px, so they stack and the chart gives up
 * the height that costs. */
#define CHART_W             UI_ROW_W
#define CHART_H             164
#else
#define CHART_W             (CONFIG_PCMON_LCD_H_RES - 24)
#define CHART_H             170
#endif

typedef struct {
    lv_obj_t            *chart;
    lv_chart_series_t   *cpu_series;
    lv_chart_series_t   *gpu_series;
    lv_obj_t            *cpu_legend;
    lv_obj_t            *gpu_legend;
    uint32_t             last_sample_ms;
} page_history_t;

static page_history_t s_page;

/** Small colour swatch + text used as a chart legend entry. */
static lv_obj_t *legend_create(lv_obj_t *parent, lv_color_t color, const char *text,
                               lv_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, color, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dot, align, x_ofs, y_ofs + 4);

    lv_obj_t *label = ui_label_create(parent, UI_FONT_TINY, UI_COLOR_TEXT, text);
    lv_obj_align(label, align, x_ofs + 13, y_ofs);

    return label;
}

static void history_create(lv_obj_t *parent)
{
#if UI_NARROW
    s_page.cpu_legend = legend_create(parent, UI_COLOR_CPU, "CPU  --%", LV_ALIGN_TOP_LEFT, 4, 0);
    s_page.gpu_legend = legend_create(parent, UI_COLOR_GPU, "GPU  --%", LV_ALIGN_TOP_LEFT, 4, 16);
#else
    s_page.cpu_legend = legend_create(parent, UI_COLOR_CPU, "CPU  --%", LV_ALIGN_TOP_LEFT, 12, 2);
    s_page.gpu_legend = legend_create(parent, UI_COLOR_GPU, "GPU  --%", LV_ALIGN_TOP_MID, 12, 2);
#endif

    s_page.chart = lv_chart_create(parent);
    lv_obj_set_size(s_page.chart, CHART_W, CHART_H);
    lv_obj_align(s_page.chart, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_remove_flag(s_page.chart, LV_OBJ_FLAG_SCROLLABLE);

    lv_chart_set_type(s_page.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_page.chart, HISTORY_POINTS);
    lv_chart_set_range(s_page.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(s_page.chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(s_page.chart, 5, 7);

    lv_obj_set_style_bg_color(s_page.chart, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_page.chart, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_page.chart, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_page.chart, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_page.chart, 4, LV_PART_MAIN);
    lv_obj_set_style_line_color(s_page.chart, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_page.chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_page.chart, 2, LV_PART_ITEMS);
    /* Data points would only add noise at this density. */
    lv_obj_set_style_size(s_page.chart, 0, 0, LV_PART_INDICATOR);

    s_page.cpu_series = lv_chart_add_series(s_page.chart, UI_COLOR_CPU, LV_CHART_AXIS_PRIMARY_Y);
    s_page.gpu_series = lv_chart_add_series(s_page.chart, UI_COLOR_GPU, LV_CHART_AXIS_PRIMARY_Y);

    lv_chart_set_all_value(s_page.chart, s_page.cpu_series, 0);
    lv_chart_set_all_value(s_page.chart, s_page.gpu_series, 0);
}

static void history_update(const pcmon_link_snapshot_t *snapshot)
{
    const pcmon_metrics_t *m       = &snapshot->metrics;
    const unsigned         cpu_pct = (m->cpu_load_pm + 5U) / 10U;
    const unsigned         gpu_pct = (m->gpu_load_pm + 5U) / 10U;

    lv_label_set_text_fmt(s_page.cpu_legend, "CPU %u%%", cpu_pct);
    lv_label_set_text_fmt(s_page.gpu_legend, "GPU %u%%", gpu_pct);

    const uint32_t now = lv_tick_get();
    if (s_page.last_sample_ms != 0 &&
        lv_tick_elaps(s_page.last_sample_ms) < HISTORY_INTERVAL_MS) {
        return;
    }
    s_page.last_sample_ms = now;

    lv_chart_set_next_value(s_page.chart, s_page.cpu_series, (int32_t)cpu_pct);
    lv_chart_set_next_value(s_page.chart, s_page.gpu_series, (int32_t)gpu_pct);
}

const ui_page_t ui_page_history = {
    .title  = "HISTORY",
    .create = history_create,
    .update = history_update,
};
