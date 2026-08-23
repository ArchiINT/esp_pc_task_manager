/*
 * PC Monitor - display back end (ST7789 over SPI + LVGL v9 port).
 */
#include "pcmon_display.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pcmon_lcd";

/* Kconfig booleans are simply absent when disabled - map them to real values. */
#ifdef CONFIG_PCMON_LCD_MIRROR_X
#define LCD_MIRROR_X    true
#else
#define LCD_MIRROR_X    false
#endif
#ifdef CONFIG_PCMON_LCD_MIRROR_Y
#define LCD_MIRROR_Y    true
#else
#define LCD_MIRROR_Y    false
#endif
#ifdef CONFIG_PCMON_LCD_SWAP_XY
#define LCD_SWAP_XY     true
#else
#define LCD_SWAP_XY     false
#endif
#ifdef CONFIG_PCMON_LCD_INVERT_COLOR
#define LCD_INVERT      true
#else
#define LCD_INVERT      false
#endif

#define LCD_SPI_HOST        SPI2_HOST
#define LCD_CMD_BITS        8
#define LCD_PARAM_BITS      8
#define LCD_BITS_PER_PIXEL  16

/* Backlight PWM: 20 kHz stays out of the audible range of cheap modules. */
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER       LEDC_TIMER_0
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BL_LEDC_RESOLUTION  LEDC_TIMER_10_BIT
#define BL_LEDC_FREQ_HZ     20000
#define BL_DUTY_MAX         ((1 << 10) - 1)

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t    panel;
    lv_display_t             *disp;
} pcmon_display_t;

static pcmon_display_t s_display;

/* -------------------------------------------------------------------------- */
/* LVGL glue                                                                   */
/* -------------------------------------------------------------------------- */

/** SPI transfer finished: let LVGL reuse the buffer.  Runs in ISR context. */
static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    (void)io;
    (void)edata;
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

/**
 * @brief LVGL flush callback: hand the rendered area to the panel via DMA.
 *
 * LVGL renders RGB565 in native (little endian) order while the panel expects
 * big endian, hence the in-place swap - a few tens of microseconds per buffer.
 */
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    lv_draw_sw_rgb565_swap(px_map, lv_area_get_size(area));

    esp_lcd_panel_draw_bitmap(s_display.panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
    (void)disp; /* flush_ready() is called from on_color_trans_done() */
}

/** LVGL time base, in milliseconds since boot. */
static uint32_t lvgl_tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* -------------------------------------------------------------------------- */
/* Hardware bring-up                                                           */
/* -------------------------------------------------------------------------- */

static esp_err_t backlight_init(void)
{
    if (CONFIG_PCMON_LCD_PIN_BL < 0) {
        return ESP_OK;
    }

    const ledc_timer_config_t timer = {
        .speed_mode      = BL_LEDC_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_RESOLUTION,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer config failed");

    const ledc_channel_config_t channel = {
        .gpio_num   = CONFIG_PCMON_LCD_PIN_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "ledc channel config failed");

    return ESP_OK;
}

void pcmon_display_set_brightness(uint8_t percent)
{
    if (CONFIG_PCMON_LCD_PIN_BL < 0) {
        return;
    }
    if (percent > 100) {
        percent = 100;
    }

    uint32_t duty = (uint32_t)percent * BL_DUTY_MAX / 100;
#if CONFIG_PCMON_LCD_BL_ACTIVE_LOW
    duty = BL_DUTY_MAX - duty;
#endif

    ESP_ERROR_CHECK(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL));
}

static esp_err_t panel_init(void)
{
    const spi_bus_config_t bus_cfg = {
        .sclk_io_num     = CONFIG_PCMON_LCD_PIN_SCLK,
        .mosi_io_num     = CONFIG_PCMON_LCD_PIN_MOSI,
        .miso_io_num     = -1, /* the panel never talks back */
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = CONFIG_PCMON_LCD_H_RES * CONFIG_PCMON_LCD_BUFFER_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init failed");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num       = CONFIG_PCMON_LCD_PIN_CS,
        .dc_gpio_num       = CONFIG_PCMON_LCD_PIN_DC,
        .spi_mode          = 0,
        .pclk_hz           = CONFIG_PCMON_LCD_PIXEL_CLOCK_MHZ * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = LCD_CMD_BITS,
        .lcd_param_bits    = LCD_PARAM_BITS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                                 &io_cfg, &s_display.io),
                        TAG, "panel io init failed");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONFIG_PCMON_LCD_PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_display.io, &panel_cfg, &s_display.panel),
                        TAG, "st7789 init failed");

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_display.panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_display.panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_display.panel, LCD_INVERT));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_display.panel, LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_display.panel, LCD_MIRROR_X, LCD_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_display.panel,
                                          CONFIG_PCMON_LCD_X_GAP,
                                          CONFIG_PCMON_LCD_Y_GAP));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_display.panel, true));

    return ESP_OK;
}

#if CONFIG_PCMON_LCD_SELFTEST

#define SELFTEST_CHUNK_LINES 20

/**
 * @brief Fill the whole panel with one colour, straight through esp_lcd.
 *
 * Runs before the LVGL flush callback exists, so esp_lcd_panel_draw_bitmap()
 * blocks until each chunk is on the wire and the buffer can be reused.
 */
static void selftest_fill(uint16_t *chunk, size_t chunk_px, uint16_t rgb565)
{
    /* The panel expects big endian, the CPU stores little endian. */
    const uint16_t swapped = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
    for (size_t i = 0; i < chunk_px; i++) {
        chunk[i] = swapped;
    }

    for (int y = 0; y < CONFIG_PCMON_LCD_V_RES; y += SELFTEST_CHUNK_LINES) {
        int y_end = y + SELFTEST_CHUNK_LINES;
        if (y_end > CONFIG_PCMON_LCD_V_RES) {
            y_end = CONFIG_PCMON_LCD_V_RES;
        }
        esp_lcd_panel_draw_bitmap(s_display.panel, 0, y, CONFIG_PCMON_LCD_H_RES, y_end, chunk);
    }
}

static void selftest_run(void)
{
    static const struct { const char *name; uint16_t rgb565; } steps[] = {
        { "red",   0xF800 },
        { "green", 0x07E0 },
        { "blue",  0x001F },
        { "white", 0xFFFF },
    };

    const size_t chunk_px = (size_t)CONFIG_PCMON_LCD_H_RES * SELFTEST_CHUNK_LINES;
    uint16_t *chunk = heap_caps_malloc(chunk_px * sizeof(uint16_t),
                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (chunk == NULL) {
        ESP_LOGE(TAG, "self-test buffer alloc failed");
        return;
    }

    /* Two passes, so the sequence is still running by the time someone has
     * plugged the cable in and looked up at the panel. */
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
            ESP_LOGW(TAG, "self-test: whole screen %s", steps[i].name);
            selftest_fill(chunk, chunk_px, steps[i].rgb565);
            vTaskDelay(pdMS_TO_TICKS(1200));
        }
    }

    ESP_LOGW(TAG, "self-test done - if the screen never changed, the panel is not "
                  "receiving SPI (check SCLK/MOSI/DC/RST and GND)");
    heap_caps_free(chunk);
}

#endif /* CONFIG_PCMON_LCD_SELFTEST */

static esp_err_t lvgl_init(void)
{
    lv_init();
    lv_tick_set_cb(lvgl_tick_get_cb);

    s_display.disp = lv_display_create(CONFIG_PCMON_LCD_H_RES, CONFIG_PCMON_LCD_V_RES);
    ESP_RETURN_ON_FALSE(s_display.disp != NULL, ESP_ERR_NO_MEM, TAG, "lv_display_create failed");

    const size_t buf_size = (size_t)CONFIG_PCMON_LCD_H_RES *
                            CONFIG_PCMON_LCD_BUFFER_LINES * sizeof(uint16_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(buf1 != NULL && buf2 != NULL, ESP_ERR_NO_MEM, TAG,
                        "draw buffer alloc failed (%u B x2)", (unsigned)buf_size);

    lv_display_set_color_format(s_display.disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display.disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display.disp, lvgl_flush_cb);

    /* Registering the callback last avoids a spurious flush_ready before the
     * display exists. */
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(s_display.io, &cbs, s_display.disp),
                        TAG, "io callback registration failed");

    ESP_LOGI(TAG, "LVGL ready, 2 x %u B draw buffers", (unsigned)buf_size);
    return ESP_OK;
}

esp_err_t pcmon_display_init(void)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight init failed");
    ESP_RETURN_ON_ERROR(panel_init(), TAG, "panel init failed");

#if CONFIG_PCMON_LCD_SELFTEST
    /* Only useful with the backlight already lit. */
    pcmon_display_set_brightness(CONFIG_PCMON_LCD_BRIGHTNESS);
    selftest_run();
#endif

    ESP_RETURN_ON_ERROR(lvgl_init(), TAG, "lvgl init failed");

    /* Backlight comes up only now, so the user never sees uninitialised RAM. */
    pcmon_display_set_brightness(CONFIG_PCMON_LCD_BRIGHTNESS);

    ESP_LOGI(TAG, "ST7789 %dx%d @ %d MHz (sclk=%d mosi=%d cs=%d dc=%d rst=%d bl=%d)",
             CONFIG_PCMON_LCD_H_RES, CONFIG_PCMON_LCD_V_RES, CONFIG_PCMON_LCD_PIXEL_CLOCK_MHZ,
             CONFIG_PCMON_LCD_PIN_SCLK, CONFIG_PCMON_LCD_PIN_MOSI, CONFIG_PCMON_LCD_PIN_CS,
             CONFIG_PCMON_LCD_PIN_DC, CONFIG_PCMON_LCD_PIN_RST, CONFIG_PCMON_LCD_PIN_BL);
    return ESP_OK;
}

lv_display_t *pcmon_display_get(void)
{
    return s_display.disp;
}
