/*
 * PC Monitor - display back end.
 *
 * Brings up the SPI bus, the ST7789 panel and the LVGL display driver
 * (double-buffered partial rendering, DMA flush, backlight on LEDC PWM).
 *
 * All LVGL calls must happen from a single task - see pcmon_ui, which owns the
 * LVGL context after pcmon_display_init() returns.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise SPI, the panel and LVGL.
 *
 * On return LVGL is initialised and a display is registered, but no timer or
 * task is running yet.
 */
esp_err_t pcmon_display_init(void);

/** @brief Set backlight brightness, 0..100 %. */
void pcmon_display_set_brightness(uint8_t percent);

/** @brief LVGL display created by pcmon_display_init(). */
lv_display_t *pcmon_display_get(void);

#ifdef __cplusplus
}
#endif
