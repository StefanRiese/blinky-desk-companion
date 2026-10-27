#include "ui/round_layout.h"

#include <algorithm>
#include <cmath>

namespace ui {

int screen_size(const lv_obj_t *obj)
{
    lv_display_t *d = lv_obj_get_display(obj);
    return d != nullptr ? lv_display_get_horizontal_resolution(d) : kDesignSize;
}

int margin(const lv_obj_t *obj)
{
    return std::max(0, (screen_size(obj) - kDesignSize) / 2);
}

void place(lv_obj_t *obj, lv_align_t align, int x, int y)
{
    const int m = margin(obj);
    switch (align) {
    case LV_ALIGN_TOP_LEFT:
        x += m;
        y += m;
        break;
    case LV_ALIGN_TOP_MID:
        y += m;
        break;
    case LV_ALIGN_TOP_RIGHT:
        x -= m;
        y += m;
        break;
    case LV_ALIGN_BOTTOM_LEFT:
        x += m;
        y -= m;
        break;
    case LV_ALIGN_BOTTOM_MID:
        y -= m;
        break;
    case LV_ALIGN_BOTTOM_RIGHT:
        x -= m;
        y -= m;
        break;
    case LV_ALIGN_LEFT_MID:
        x += m;
        break;
    case LV_ALIGN_RIGHT_MID:
        x -= m;
        break;
    default: // centred: nothing to move
        break;
    }
    lv_obj_align(obj, align, x, y);
}

int visible_width(int diameter, int y, int margin)
{
    const double r = diameter / 2.0;
    const double dy = std::abs((y + 0.5) - r); // distance of the row's centre from the middle
    if (dy >= r) {
        return 0;
    }
    const int w = static_cast<int>(2.0 * std::sqrt(r * r - dy * dy)) - 2 * margin;
    return std::max(w, 0);
}

bool fits(int diameter, int y, int w, int h, int margin)
{
    if (y < 0 || h <= 0 || y + h > diameter) {
        return false;
    }
    // A centred box fits if its narrowest row (the one farthest from the middle) fits.
    return visible_width(diameter, y, margin) >= w && visible_width(diameter, y + h - 1, margin) >= w;
}

} // namespace ui
