// Small builders the pages share: a full-screen container, a label, the page title and a
// label setter that also shows or hides it. Used by the pages in this folder, with set.h.
#pragma once

#include <cstring>
#include <string>

#include "lvgl.h"
#include "set.h"
#include "ui/fonts.h"
#include "ui/round_layout.h"
#include "ui/theme.h"

namespace ui {

// kTitleY is where every page's title sits (on the 412 px design), below the status bar.
constexpr int kTitleY = 84;

// page is a full-screen box for a page's widgets; touches go to the screen.
inline lv_obj_t *page(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_clickable(p, false);
    return p;
}

inline lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text = "")
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    set_text_color(l, color);
    set_text(l, text);
    return l;
}

// title is a page's name in small spaced capitals at the top ("FORECAST").
inline lv_obj_t *title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *t = label(parent, font::small(), theme::muted(), text);
    lv_obj_set_style_text_letter_space(t, 2, 0);
    place(t, LV_ALIGN_TOP_MID, 0, kTitleY);
    return t;
}

inline void set_text(lv_obj_t *l, const std::string &s)
{
    set_text(l, s.c_str());
}

// set shows or hides a label; a shown one gets text and color, each only if it differs.
inline void set(lv_obj_t *l, bool shown, const char *text = "", lv_color_t color = theme::text())
{
    lv_obj_set_hidden(l, !shown);
    if (shown) {
        set_text(l, text);
        set_text_color(l, color);
    }
}

} // namespace ui
