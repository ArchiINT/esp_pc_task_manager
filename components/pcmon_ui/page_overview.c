/*
 * PC Monitor - overview page: CPU and GPU load side by side, memory below.
 */
#include <stdio.h>

#include "ui_page.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#if UI_NARROW
/* Two 62 px dials still sit side by side on a 135 px panel; anything larger has
 * to stack, which would push the memory bars off the bottom. */
#define GAUGE_SIZE  62
#define GAUGE_ARC_W 7
#define GAUGE_FONT  UI_FONT_SMALL
#define GAUGE_X     2
#define GAUGE_Y     0
#define INFO_Y      64
#define RAM_Y       86
#define VRAM_Y      120
#elif UI_TALL
/* 240x320: two 110 px dials still fit across the 240 px width with an 8 px
 * gutter, and the freed height goes into spacing the two memory bars out. */
#define GAUGE_SIZE  110
#define GAUGE_ARC_W 10
#define GAUGE_FONT  UI_FONT_LARGE
#define GAUGE_X     6
#define GAUGE_Y     8
#define INFO_Y      122
#define RAM_Y       160
#define VRAM_Y      216
#else
#define GAUGE_SIZE  102
#define GAUGE_ARC_W 9
#define GAUGE_FONT  UI_FONT_LARGE
#define GAUGE_X     8
#define GAUGE_Y     0
#define RAM_Y       106
#define VRAM_Y      138
#endif

#define METER_W     UI_ROW_W

typedef struct {
    ui_gauge_t cpu;
    ui_gauge_t gpu;
    ui_meter_t ram;
    ui_meter_t vram;
    lv_obj_t  *cpu_clock;
    lv_obj_t  *gpu_power;
} page_overview_t;

static page_overview_t s_page;

static void overview_create(lv_obj_t *parent)
{
    ui_gauge_create(&s_page.cpu, parent, GAUGE_SIZE, UI_COLOR_CPU, "CPU",
                    GAUGE_FONT, GAUGE_ARC_W);
    lv_obj_align(s_page.cpu.root, LV_ALIGN_TOP_LEFT, GAUGE_X, GAUGE_Y);

    ui_gauge_create(&s_page.gpu, parent, GAUGE_SIZE, UI_COLOR_GPU, "GPU",
                    GAUGE_FONT, GAUGE_ARC_W);
    lv_obj_align(s_page.gpu.root, LV_ALIGN_TOP_RIGHT, -GAUGE_X, GAUGE_Y);

    ui_meter_create(&s_page.ram, parent, METER_W, UI_COLOR_RAM, "RAM");
    lv_obj_align(s_page.ram.root, LV_ALIGN_TOP_MID, 0, RAM_Y);

    ui_meter_create(&s_page.vram, parent, METER_W, UI_COLOR_VRAM, "VRAM");
    lv_obj_align(s_page.vram.root, LV_ALIGN_TOP_MID, 0, VRAM_Y);

    s_page.cpu_clock = ui_label_create(parent, UI_FONT_TINY, UI_COLOR_TEXT_DIM, "-- GHz");
    s_page.gpu_power = ui_label_create(parent, UI_FONT_TINY, UI_COLOR_TEXT_DIM, "-- W");

#if UI_NARROW || UI_TALL
    /* Straight under the dials: the bottom of the page belongs to the bars. */
    lv_obj_align(s_page.cpu_clock, LV_ALIGN_TOP_LEFT, GAUGE_X, INFO_Y);
    lv_obj_align(s_page.gpu_power, LV_ALIGN_TOP_RIGHT, -GAUGE_X, INFO_Y);
#else
    lv_obj_align(s_page.cpu_clock, LV_ALIGN_BOTTOM_LEFT, 8, -2);
    lv_obj_align(s_page.gpu_power, LV_ALIGN_BOTTOM_RIGHT, -8, -2);
#endif
}

static void overview_update(const pcmon_link_snapshot_t *snapshot)
{
    const pcmon_metrics_t *m = &snapshot->metrics;
    char                   buf[24];

    /* CPU */
    ui_gauge_set_load(&s_page.cpu, m->cpu_load_pm);
    if (m->flags & PCMON_FLAG_CPU_TEMP_VALID) {
        snprintf(buf, sizeof(buf), "%d°C", m->cpu_temp_dc / 10);
        ui_gauge_set_sub(&s_page.cpu, buf, ui_temp_color(m->cpu_temp_dc));
    } else {
        ui_gauge_set_sub(&s_page.cpu, "--", UI_COLOR_TEXT_DIM);
    }
    lv_label_set_text_fmt(s_page.cpu_clock, "%u.%02u GHz",
                          (unsigned)(m->cpu_freq_mhz / 1000U),
                          (unsigned)((m->cpu_freq_mhz % 1000U) / 10U));

    /* GPU */
    if (m->flags & PCMON_FLAG_GPU_PRESENT) {
        ui_gauge_set_load(&s_page.gpu, m->gpu_load_pm);
        if (m->flags & PCMON_FLAG_GPU_TEMP_VALID) {
            snprintf(buf, sizeof(buf), "%d°C", m->gpu_temp_dc / 10);
            ui_gauge_set_sub(&s_page.gpu, buf, ui_temp_color(m->gpu_temp_dc));
        } else {
            ui_gauge_set_sub(&s_page.gpu, "--", UI_COLOR_TEXT_DIM);
        }
        lv_label_set_text_fmt(s_page.gpu_power, "%u W", (unsigned)(m->gpu_power_dw / 10U));
        ui_meter_set(&s_page.vram, m->vram_used_mb, m->vram_total_mb);
    } else {
        lv_label_set_text(s_page.gpu.value, "N/A");
        ui_gauge_set_sub(&s_page.gpu, "no GPU", UI_COLOR_TEXT_DIM);
        lv_label_set_text(s_page.gpu_power, "--");
    }

    /* Memory */
    ui_meter_set(&s_page.ram, m->ram_used_mb, m->ram_total_mb);
}

const ui_page_t ui_page_overview = {
    .title  = "OVERVIEW",
    .create = overview_create,
    .update = overview_update,
};
