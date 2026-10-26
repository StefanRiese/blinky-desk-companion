// Geometry helpers for round screens: how much width is visible at a given height.
#pragma once

namespace ui {

// visible_width returns the width of the visible circle (diameter d) at row y
// (0 = top), minus margin on each side; 0 outside the circle.
int visible_width(int diameter, int y, int margin = 0);

// fits reports whether a w×h box centred horizontally with its top at y lies completely
// inside the circle (diameter d), keeping margin to the edge.
bool fits(int diameter, int y, int w, int h, int margin = 0);

} // namespace ui
