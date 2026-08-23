/*
 * PC Monitor - page interface.
 *
 * A page owns a container that fills the content area between the header and
 * the page indicator.  The page manager creates every page once at boot and
 * only toggles visibility, so switching pages costs a single redraw.
 *
 * update() is called for every snapshot, including for hidden pages: keeping
 * them current is a handful of string formats and lets the history page collect
 * data while it is not on screen.
 */
#pragma once

#include "lvgl.h"
#include "pcmon_link.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *title;
    void (*create)(lv_obj_t *parent);
    void (*update)(const pcmon_link_snapshot_t *snapshot);
} ui_page_t;

extern const ui_page_t ui_page_overview;
extern const ui_page_t ui_page_cpu;
extern const ui_page_t ui_page_gpu;
extern const ui_page_t ui_page_history;

#ifdef __cplusplus
}
#endif
