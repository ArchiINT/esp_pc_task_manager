/*
 * PC Monitor - UI task and page manager.
 *
 * Screen layout (240x320; the 240x240 and 135x240 panels use the same three
 * bands with a shorter header, footer and content area - see ui_theme.h):
 *
 *   +--------------------------------------+  0
 *   | * HOSTNAME                  04:21:07 |  header, 28 px
 *   +--------------------------------------+  28
 *   |                                      |
 *   |   active page (all pages stacked,    |  content, 278 px
 *   |   inactive ones simply hidden)       |
 *   |                                      |
 *   +--------------------------------------+  306
 *   |             *  o  o  o               |  page indicator, 14 px
 *   +--------------------------------------+  320
 */
#include "pcmon_ui.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pcmon_display.h"
#include "pcmon_link.h"
#include "ui_page.h"
#include "ui_theme.h"
#include "ui_widgets.h"

static const char *TAG = "pcmon_ui";

/* lv_timer_handler() is polled at least this often so that animations and the
 * button stay responsive; it never sleeps longer than this either. */
#define UI_MIN_IDLE_MS      5
#define UI_MAX_IDLE_MS      30
#define BUTTON_DEBOUNCE_MS  40
#define BUTTON_LONG_MS      800

/* Dual core parts (ESP32-S3) keep the radio/USB ISRs on core 0 and render on
 * core 1; single core parts (ESP32-C3) only have core 0, and asking for core 1
 * there trips an assert inside xTaskCreatePinnedToCore(). */
#if CONFIG_FREERTOS_UNICORE
#define UI_TASK_CORE        0
#else
#define UI_TASK_CORE        1
#endif

/* Header: the uptime is the one field that must never be truncated, so it gets
 * its budget first and the hostname takes what is left. */
#if UI_NARROW
#define HEADER_UPTIME_FONT  UI_FONT_TINY
#define HEADER_UPTIME_W     58
#elif UI_TALL
/* The 28 px header of the 2.8" panel has room for a 16 px face; the hostname
 * stays small so it keeps as many characters as before. */
#define HEADER_UPTIME_FONT  UI_FONT_MEDIUM
#define HEADER_UPTIME_W     92
#else
#define HEADER_UPTIME_FONT  UI_FONT_SMALL
#define HEADER_UPTIME_W     78
#endif

/* Link dot and page dots scale with the band they live in. */
#if UI_TALL
#define HEADER_DOT_D        10
#define HEADER_HOST_X       26
#define PAGE_DOT_D          8
#define PAGE_DOT_SPACING    16
#else
#define HEADER_DOT_D        8
#define HEADER_HOST_X       22
#define PAGE_DOT_D          6
#define PAGE_DOT_SPACING    12
#endif

static const ui_page_t *const s_pages[] = {
    &ui_page_overview,
    &ui_page_cpu,
    &ui_page_gpu,
    &ui_page_history,
};

#define PAGE_COUNT (sizeof(s_pages) / sizeof(s_pages[0]))

typedef struct {
    lv_obj_t *containers[PAGE_COUNT];
    lv_obj_t *dots[PAGE_COUNT];
    uint8_t   current;

    lv_obj_t *host_label;
    lv_obj_t *uptime_label;
    lv_obj_t *link_dot;
    lv_obj_t *overlay;
    lv_obj_t *overlay_hint;

    bool     autocycle;
    uint32_t last_switch_ms;

    /* Button state machine */
    bool     btn_pressed;
    bool     btn_long_fired;
    uint32_t btn_change_ms;

    pcmon_link_snapshot_t snapshot;
} pcmon_ui_t;

static pcmon_ui_t s_ui;

/* -------------------------------------------------------------------------- */
/* Screen construction                                                         */
/* -------------------------------------------------------------------------- */

static void header_create(lv_obj_t *screen)
{
    lv_obj_t *header = ui_container_create(screen, CONFIG_PCMON_LCD_H_RES, UI_HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);

    s_ui.link_dot = lv_obj_create(header);
    lv_obj_set_size(s_ui.link_dot, HEADER_DOT_D, HEADER_DOT_D);
    lv_obj_set_style_radius(s_ui.link_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.link_dot, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.link_dot, UI_COLOR_DANGER, LV_PART_MAIN);
    lv_obj_remove_flag(s_ui.link_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_ui.link_dot, LV_ALIGN_LEFT_MID, 8, 0);

    s_ui.host_label = ui_label_create(header, UI_FONT_TINY, UI_COLOR_TEXT_DIM,
                                      pcmon_link_transport_name());
    /* Bounded width plus ellipsis: a long hostname must not slide under the
     * uptime, which has no room to spare on a narrow panel. */
    lv_obj_set_width(s_ui.host_label,
                     CONFIG_PCMON_LCD_H_RES - HEADER_HOST_X - HEADER_UPTIME_W - 6);
    lv_label_set_long_mode(s_ui.host_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(s_ui.host_label, LV_ALIGN_LEFT_MID, HEADER_HOST_X, 0);

    s_ui.uptime_label = ui_label_create(header, HEADER_UPTIME_FONT, UI_COLOR_TEXT, "--:--:--");
    lv_obj_align(s_ui.uptime_label, LV_ALIGN_RIGHT_MID, -8, 0);
}

static void indicator_create(lv_obj_t *screen)
{
    lv_obj_t *bar = ui_container_create(screen, CONFIG_PCMON_LCD_H_RES, UI_FOOTER_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);

    const int32_t spacing = PAGE_DOT_SPACING;
    const int32_t x0      = -(int32_t)(PAGE_COUNT - 1) * spacing / 2;

    for (size_t i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(bar);
        lv_obj_set_size(dot, PAGE_DOT_D, PAGE_DOT_D);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, UI_COLOR_TRACK, LV_PART_MAIN);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(dot, LV_ALIGN_CENTER, x0 + (int32_t)i * spacing, 0);
        s_ui.dots[i] = dot;
    }
}

static void overlay_create(lv_obj_t *screen)
{
    s_ui.overlay = ui_container_create(screen, CONFIG_PCMON_LCD_H_RES, UI_CONTENT_H);
    lv_obj_align(s_ui.overlay, LV_ALIGN_TOP_MID, 0, UI_CONTENT_Y);
    lv_obj_set_style_bg_color(s_ui.overlay, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.overlay, LV_OPA_90, LV_PART_MAIN);

    lv_obj_t *title = ui_label_create(s_ui.overlay, UI_FONT_MEDIUM, UI_COLOR_TEXT,
                                      "WAITING FOR HOST");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -12);

    char hint[64];
    snprintf(hint, sizeof(hint), "start pcmon_agent.py  (%s)", pcmon_link_transport_name());
    s_ui.overlay_hint = ui_label_create(s_ui.overlay, UI_FONT_TINY, UI_COLOR_TEXT_DIM, hint);
    lv_obj_align(s_ui.overlay_hint, LV_ALIGN_CENTER, 0, 10);
}

static void page_select(uint8_t index)
{
    if (index >= PAGE_COUNT) {
        index = 0;
    }

    lv_obj_add_flag(s_ui.containers[s_ui.current], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_ui.dots[s_ui.current], UI_COLOR_TRACK, LV_PART_MAIN);

    s_ui.current = index;

    lv_obj_remove_flag(s_ui.containers[index], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_ui.dots[index], UI_COLOR_TEXT, LV_PART_MAIN);

    s_ui.last_switch_ms = lv_tick_get();
}

static void screen_create(void)
{
    lv_obj_t *screen = lv_screen_active();

    lv_obj_set_style_bg_color(screen, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    header_create(screen);

    for (size_t i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *page = ui_container_create(screen, CONFIG_PCMON_LCD_H_RES, UI_CONTENT_H);
        lv_obj_align(page, LV_ALIGN_TOP_MID, 0, UI_CONTENT_Y);
        lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
        s_pages[i]->create(page);
        s_ui.containers[i] = page;
    }

    indicator_create(screen);
    overlay_create(screen); /* created last so it stays on top */

    s_ui.current = PAGE_COUNT - 1; /* forces page_select() to switch properly */
    page_select(0);
}

/* -------------------------------------------------------------------------- */
/* Runtime                                                                     */
/* -------------------------------------------------------------------------- */

/** Refresh the always-visible parts and hand the sample to every page. */
static void ui_apply_snapshot(const pcmon_link_snapshot_t *snapshot)
{
    const bool up = (snapshot->state == PCMON_LINK_UP);

    lv_obj_set_style_bg_color(s_ui.link_dot, up ? UI_COLOR_RAM : UI_COLOR_DANGER, LV_PART_MAIN);

    if (snapshot->hello_valid) {
        lv_label_set_text(s_ui.host_label, snapshot->hello.hostname);
    }

    char uptime[16];
    ui_format_uptime(uptime, sizeof(uptime), snapshot->metrics.uptime_s);
    lv_label_set_text(s_ui.uptime_label, uptime);

    if (up) {
        lv_obj_add_flag(s_ui.overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_ui.overlay, LV_OBJ_FLAG_HIDDEN);
    }

    for (size_t i = 0; i < PAGE_COUNT; i++) {
        s_pages[i]->update(snapshot);
    }
}

/* The pin number is a compile-time constant, so the whole button handling is
 * compiled out when it is disabled (a negative shift would not even build). */
#if CONFIG_PCMON_BUTTON_GPIO >= 0

static void button_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_PCMON_BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

/**
 * @brief Debounced polling of the page button.
 *
 * Short press -> next page.  Long press toggles automatic cycling, and only
 * exists when a cycling interval is configured - otherwise there is nothing to
 * toggle and holding the button simply does nothing.
 */
static void button_poll(void)
{
    const bool     pressed = (gpio_get_level(CONFIG_PCMON_BUTTON_GPIO) == 0);
    const uint32_t now     = lv_tick_get();

    if (pressed != s_ui.btn_pressed) {
        if (lv_tick_elaps(s_ui.btn_change_ms) < BUTTON_DEBOUNCE_MS) {
            return;
        }
        s_ui.btn_change_ms = now;
        s_ui.btn_pressed   = pressed;

        if (pressed) {
            s_ui.btn_long_fired = false;
        } else if (!s_ui.btn_long_fired) {
            page_select((uint8_t)((s_ui.current + 1) % PAGE_COUNT));
        }
        return;
    }

#if CONFIG_PCMON_UI_AUTOCYCLE_MS > 0
    if (pressed && !s_ui.btn_long_fired && lv_tick_elaps(s_ui.btn_change_ms) >= BUTTON_LONG_MS) {
        s_ui.btn_long_fired = true;
        s_ui.autocycle      = !s_ui.autocycle;
        s_ui.last_switch_ms = now;
        ESP_LOGI(TAG, "auto page cycling %s", s_ui.autocycle ? "on" : "off");
    }
#else
    (void)now;
#endif
}

#else /* button disabled */

static void button_init(void) {}
static void button_poll(void) {}

#endif /* CONFIG_PCMON_BUTTON_GPIO >= 0 */

/* Same pattern as the button above: with the interval set to zero there is
 * nothing to poll, so the whole thing is compiled out. */
#if CONFIG_PCMON_UI_AUTOCYCLE_MS > 0

static void autocycle_poll(void)
{
    if (!s_ui.autocycle) {
        return;
    }
    if (lv_tick_elaps(s_ui.last_switch_ms) >= CONFIG_PCMON_UI_AUTOCYCLE_MS) {
        page_select((uint8_t)((s_ui.current + 1) % PAGE_COUNT));
    }
}

#else
static void autocycle_poll(void) {}
#endif

static void ui_task(void *arg)
{
    (void)arg;

    screen_create();
    ui_apply_snapshot(&s_ui.snapshot); /* paint the "no host" state immediately */

    ESP_LOGI(TAG, "UI running (%u pages)", (unsigned)PAGE_COUNT);

    for (;;) {
        uint32_t idle_ms = lv_timer_handler();
        if (idle_ms < UI_MIN_IDLE_MS) {
            idle_ms = UI_MIN_IDLE_MS;
        } else if (idle_ms > UI_MAX_IDLE_MS) {
            idle_ms = UI_MAX_IDLE_MS;
        }

        /* Sleeping on the link mailbox means a fresh sample is drawn within one
         * LVGL cycle instead of waiting for the next poll tick. */
        if (pcmon_link_wait(&s_ui.snapshot, pdMS_TO_TICKS(idle_ms))) {
            ui_apply_snapshot(&s_ui.snapshot);
        }

        button_poll();
        autocycle_poll();
    }
}

esp_err_t pcmon_ui_start(void)
{
    s_ui.autocycle       = (CONFIG_PCMON_UI_AUTOCYCLE_MS > 0);
    s_ui.snapshot.state  = PCMON_LINK_DOWN;

    button_init();

    const BaseType_t ok = xTaskCreatePinnedToCore(ui_task, "pcmon_ui",
                                                  CONFIG_PCMON_UI_TASK_STACK, NULL,
                                                  CONFIG_PCMON_UI_TASK_PRIO, NULL,
                                                  UI_TASK_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "UI task create failed");

    return ESP_OK;
}
