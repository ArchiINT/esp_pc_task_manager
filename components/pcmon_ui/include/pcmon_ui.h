/*
 * PC Monitor - user interface.
 *
 * Owns the LVGL context: creates the screen, runs lv_timer_handler() and feeds
 * the pages with link snapshots.  Everything LVGL related happens inside the UI
 * task, which removes the need for a global LVGL lock.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the interface and start the UI task.
 *
 * Call after pcmon_display_init() and pcmon_link_start().
 */
esp_err_t pcmon_ui_start(void);

#ifdef __cplusplus
}
#endif
