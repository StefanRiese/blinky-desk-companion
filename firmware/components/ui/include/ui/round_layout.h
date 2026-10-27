// Geometry helpers for round screens: how much width is visible at a given height, and how
// layouts made for the 412 px screen stay centred on larger ones.
#pragma once

#include "lvgl.h"

namespace ui {

// Layouts are drawn for a 412×412 screen. Larger round screens (the 466×466 AMOLED) have
// about the same pixel density, so sizes stay the same and the layout stays centred: place
// moves objects aligned to an edge by half the difference, inwards.
constexpr int kDesignSize = 412;

// screen_size returns the width of the display obj is on.
int screen_size(const lv_obj_t *obj);
// margin returns (screen size − 412) / 2 for obj's display.
int margin(const lv_obj_t *obj);
// place aligns obj like lv_obj_align with coordinates for the 412 px screen.
void place(lv_obj_t *obj, lv_align_t align, int x, int y);

// visible_width returns the width of the visible circle (diameter d) at row y
// (0 = top), minus margin on each side; 0 outside the circle.
int visible_width(int diameter, int y, int margin = 0);

// fits reports whether a w×h box centred horizontally with its top at y lies completely
// inside the circle (diameter d), keeping margin to the edge.
bool fits(int diameter, int y, int w, int h, int margin = 0);

} // namespace ui
