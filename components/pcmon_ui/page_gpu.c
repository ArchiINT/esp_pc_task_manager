/*
 * PC Monitor - GPU page: NVIDIA core load, temperature, clock, board power,
 * video memory and fan speed.
 */
#include <stdio.h>

#include "ui_page.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define ROW_W       UI_ROW_W

#if UI_NARROW
/* A smaller dial than the CPU page uses: this page has to fit two stat rows
 * and two meters underneath it. */
#define GAUGE_SIZE  84
#define GAUGE_ARC_W 9
#define STAT_W      ROW_W
#define CLOCK_Y     100
#define POWER_Y     120
#define VRAM_Y      142
#define FAN_Y       174
#else
#define GAUGE_SIZE  104
#define GAUGE_ARC_W 11
#define STAT_W      ((ROW_W - 8) / 2)
#define CLOCK_Y     122
#define POWER_Y     122
#define VRAM_Y      142
#define FAN_Y       174
#endif

typedef struct {
    ui_gauge_t load;
    ui_stat_t  clock;
    ui_stat_t  power;
    ui_meter_t vram;
    ui_meter_t fan;
    lv_obj_t  *model;
} page_gpu_t;

static page_gpu_t s_page;

static void gpu_create(lv_obj_t *parent)
{
    s_page.model = ui_label_create(parent, UI_FONT_TINY, UI_COLOR_TEXT_DIM, "GPU");
    lv_obj_set_width(s_page.model, ROW_W);
    lv_label_set_long_mode(s_page.model, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(s_page.model, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_page.model, LV_ALIGN_TOP_MID, 0, 0);

    ui_gauge_create(&s_page.load, parent, GAUGE_SIZE, UI_COLOR_GPU, "LOAD",
                    UI_FONT_LARGE, GAUGE_ARC_W);
    lv_obj_align(s_page.load.root, LV_ALIGN_TOP_MID, 0, 14);

    ui_stat_create(&s_page.clock, parent, STAT_W, "CLOCK");
    ui_stat_create(&s_page.power, parent, STAT_W, "POWER");
#if UI_NARROW
    lv_obj_align(s_page.clock.root, LV_ALIGN_TOP_MID, 0, CLOCK_Y);
    lv_obj_align(s_page.power.root, LV_ALIGN_TOP_MID, 0, POWER_Y);
#else
    lv_obj_align(s_page.clock.root, LV_ALIGN_TOP_LEFT, 16, CLOCK_Y);
    lv_obj_align(s_page.power.root, LV_ALIGN_TOP_RIGHT, -16, POWER_Y);
#endif

    ui_meter_create(&s_page.vram, parent, ROW_W, UI_COLOR_VRAM, "VRAM");
    lv_obj_align(s_page.vram.root, LV_ALIGN_TOP_MID, 0, VRAM_Y);

    ui_meter_create(&s_page.fan, parent, ROW_W, UI_COLOR_GPU, "FAN");
    lv_obj_align(s_page.fan.root, LV_ALIGN_TOP_MID, 0, FAN_Y);
}

static void gpu_update(const pcmon_link_snapshot_t *snapshot)
{
    const pcmon_metrics_t *m = &snapshot->metrics;

    if (snapshot->hello_valid && snapshot->hello.gpu_name[0] != '\0') {
        lv_label_set_text(s_page.model, snapshot->hello.gpu_name);
    }

    if (!(m->flags & PCMON_FLAG_GPU_PRESENT)) {
        lv_label_set_text(s_page.load.value, "N/A");
        ui_gauge_set_sub(&s_page.load, "no NVIDIA GPU", UI_COLOR_TEXT_DIM);
        lv_label_set_text(s_page.clock.value, "--");
        lv_label_set_text(s_page.power.value, "--");
        ui_meter_set_pm(&s_page.vram, 0, "--");
        ui_meter_set_pm(&s_page.fan, 0, "--");
        return;
    }

    ui_gauge_set_load(&s_page.load, m->gpu_load_pm);
    if (m->flags & PCMON_FLAG_GPU_TEMP_VALID) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d°C", m->gpu_temp_dc / 10);
        ui_gauge_set_sub(&s_page.load, buf, ui_temp_color(m->gpu_temp_dc));
    }

    lv_label_set_text_fmt(s_page.clock.value, "%u MHz", (unsigned)m->gpu_freq_mhz);

    if (m->flags & PCMON_FLAG_GPU_POWER_VALID) {
        lv_label_set_text_fmt(s_page.power.value, "%u.%u W",
                              (unsigned)(m->gpu_power_dw / 10U),
                              (unsigned)(m->gpu_power_dw % 10U));
    } else {
        lv_label_set_text(s_page.power.value, "--");
    }

    ui_meter_set(&s_page.vram, m->vram_used_mb, m->vram_total_mb);

    if (m->flags & PCMON_FLAG_GPU_FAN_VALID) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)((m->gpu_fan_pm + 5U) / 10U));
        ui_meter_set_pm(&s_page.fan, m->gpu_fan_pm, buf);
    } else {
        ui_meter_set_pm(&s_page.fan, 0, "--");
    }
}

const ui_page_t ui_page_gpu = {
    .title  = "GPU",
    .create = gpu_create,
    .update = gpu_update,
};
