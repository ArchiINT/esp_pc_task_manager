/*
 * PC Monitor - theme helpers.
 */
#include "ui_theme.h"

#include <stdio.h>

lv_color_t ui_temp_color(int16_t temp_dc)
{
    if (temp_dc >= 850) {
        return UI_COLOR_DANGER;
    }
    if (temp_dc >= 700) {
        return UI_COLOR_WARN;
    }
    if (temp_dc >= 500) {
        return UI_COLOR_TEXT;
    }
    return UI_COLOR_RAM;
}

lv_color_t ui_load_color(lv_color_t base, uint16_t load_pm)
{
    if (load_pm >= 950) {
        return UI_COLOR_DANGER;
    }
    if (load_pm >= 850) {
        return UI_COLOR_WARN;
    }
    return base;
}

void ui_format_uptime(char *buf, size_t size, uint32_t seconds)
{
    const uint32_t days  = seconds / 86400U;
    const uint32_t hours = (seconds % 86400U) / 3600U;
    const uint32_t mins  = (seconds % 3600U) / 60U;
    const uint32_t secs  = seconds % 60U;

    if (days > 0) {
        snprintf(buf, size, "%ud %02u:%02u", (unsigned)days, (unsigned)hours, (unsigned)mins);
    } else {
        snprintf(buf, size, "%02u:%02u:%02u", (unsigned)hours, (unsigned)mins, (unsigned)secs);
    }
}

void ui_format_mib(char *buf, size_t size, uint32_t mib)
{
    if (mib >= 1024U) {
        /* One decimal is enough at this text size, and avoids float printf. */
        const uint32_t whole = mib / 1024U;
        const uint32_t tenth = ((mib % 1024U) * 10U) / 1024U;
        snprintf(buf, size, "%u.%uG", (unsigned)whole, (unsigned)tenth);
    } else {
        snprintf(buf, size, "%uM", (unsigned)mib);
    }
}

lv_obj_t *ui_container_create(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    return obj;
}

lv_obj_t *ui_card_create(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *card = ui_container_create(parent, w, h);

    lv_obj_set_style_bg_color(card, UI_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 6, LV_PART_MAIN);

    return card;
}

lv_obj_t *ui_label_create(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                          const char *text)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text);

    return label;
}
