// Setters that leave a widget alone when nothing changes. LVGL redraws a label on every
// lv_label_set_text and every style change, even with the same value, and the pages update
// their widgets on every tick: on the device that kept the CPU busy on pages where nothing
// moved. Used by the pages in this folder.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "lvgl.h"

namespace ui {

inline void set_text(lv_obj_t *label, const char *text)
{
    if (std::strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

[[gnu::format(printf, 2, 3)]] inline void set_text_fmt(lv_obj_t *label, const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    set_text(label, buf);
}

inline void set_text_color(lv_obj_t *obj, lv_color_t c, lv_part_t part = LV_PART_MAIN)
{
    if (!lv_color_eq(lv_obj_get_style_text_color(obj, part), c)) {
        lv_obj_set_style_text_color(obj, c, part);
    }
}

inline void set_bg_color(lv_obj_t *obj, lv_color_t c, lv_part_t part = LV_PART_MAIN)
{
    if (!lv_color_eq(lv_obj_get_style_bg_color(obj, part), c)) {
        lv_obj_set_style_bg_color(obj, c, part);
    }
}

inline void set_line_color(lv_obj_t *obj, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_line_color(obj, LV_PART_MAIN), c)) {
        lv_obj_set_style_line_color(obj, c, LV_PART_MAIN);
    }
}

inline void set_border_color(lv_obj_t *obj, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_border_color(obj, LV_PART_MAIN), c)) {
        lv_obj_set_style_border_color(obj, c, LV_PART_MAIN);
    }
}

inline void set_arc_color(lv_obj_t *obj, lv_color_t c, lv_part_t part = LV_PART_MAIN)
{
    if (!lv_color_eq(lv_obj_get_style_arc_color(obj, part), c)) {
        lv_obj_set_style_arc_color(obj, c, part);
    }
}

} // namespace ui
