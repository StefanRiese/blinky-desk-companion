#include "ui/round_layout.h"

#include <algorithm>
#include <cmath>

namespace ui {

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
