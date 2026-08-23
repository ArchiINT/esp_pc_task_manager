/*
 * PC Monitor - CPU page: total load, clock, package temperature and a bar per
 * logical core.
 */
#include <stdio.h>

#include "ui_page.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define ROW_W        UI_ROW_W

#if UI_NARROW
/* CLOCK and TEMP get a full-width row each: at 135 px a half-width row leaves
 * about 47 px, which is not enough for "4.20 GHz" next to its caption. */
#define GAUGE_SIZE   100
#define GAUGE_ARC_W  10
#define STAT_W       ROW_W
#define CLOCK_Y      120
#define TEMP_Y       140
#define CORES_H      44
#else
#define GAUGE_SIZE   112
#define GAUGE_ARC_W  11
#define STAT_W       ((ROW_W - 8) / 2)
#define CLOCK_Y      130
#define TEMP_Y       130
#define CORES_H      52
#endif

typedef struct {
    ui_gauge_t    load;
    ui_stat_t     clock;
    ui_stat_t     temp;
    ui_corebars_t cores;
    lv_obj_t     *model;
} page_cpu_t;

static page_cpu_t s_page;

static void cpu_create(lv_obj_t *parent)
{
    s_page.model = ui_label_create(parent, UI_FONT_TINY, UI_COLOR_TEXT_DIM, "CPU");
    lv_obj_set_width(s_page.model, ROW_W);
    lv_label_set_long_mode(s_page.model, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(s_page.model, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_page.model, LV_ALIGN_TOP_MID, 0, 0);

    ui_gauge_create(&s_page.load, parent, GAUGE_SIZE, UI_COLOR_CPU, "LOAD",
                    UI_FONT_HUGE, GAUGE_ARC_W);
    lv_obj_align(s_page.load.root, LV_ALIGN_TOP_MID, 0, 14);

    ui_stat_create(&s_page.clock, parent, STAT_W, "CLOCK");
    ui_stat_create(&s_page.temp, parent, STAT_W, "TEMP");
#if UI_NARROW
    lv_obj_align(s_page.clock.root, LV_ALIGN_TOP_MID, 0, CLOCK_Y);
    lv_obj_align(s_page.temp.root, LV_ALIGN_TOP_MID, 0, TEMP_Y);
#else
    lv_obj_align(s_page.clock.root, LV_ALIGN_TOP_LEFT, 16, CLOCK_Y);
    lv_obj_align(s_page.temp.root, LV_ALIGN_TOP_RIGHT, -16, TEMP_Y);
#endif

    ui_corebars_create(&s_page.cores, parent, ROW_W, CORES_H, UI_COLOR_CPU);
    lv_obj_align(s_page.cores.root, LV_ALIGN_BOTTOM_MID, 0, -2);
}

static void cpu_update(const pcmon_link_snapshot_t *snapshot)
{
    const pcmon_metrics_t *m = &snapshot->metrics;

    ui_gauge_set_load(&s_page.load, m->cpu_load_pm);
    lv_label_set_text_fmt(s_page.load.sub, "%u threads", (unsigned)m->core_count);

    lv_label_set_text_fmt(s_page.clock.value, "%u.%02u GHz",
                          (unsigned)(m->cpu_freq_mhz / 1000U),
                          (unsigned)((m->cpu_freq_mhz % 1000U) / 10U));

    if (m->flags & PCMON_FLAG_CPU_TEMP_VALID) {
        lv_label_set_text_fmt(s_page.temp.value, "%d°C", m->cpu_temp_dc / 10);
        lv_obj_set_style_text_color(s_page.temp.value, ui_temp_color(m->cpu_temp_dc), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_page.temp.value, "--");
        lv_obj_set_style_text_color(s_page.temp.value, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    }

    ui_corebars_set(&s_page.cores, m->core_load_pct, m->core_count);

    if (snapshot->hello_valid) {
        lv_label_set_text(s_page.model, snapshot->hello.cpu_name);
    }
}

const ui_page_t ui_page_cpu = {
    .title  = "CPU",
    .create = cpu_create,
    .update = cpu_update,
};
