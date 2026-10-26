// Colours and sizes of the Blinky UI (same tokens as the browser emulator).
#pragma once

#include "lvgl.h"

namespace ui::theme {

inline lv_color_t background() { return lv_color_hex(0x000000); }
inline lv_color_t text() { return lv_color_hex(0xF2F4F7); }
inline lv_color_t muted() { return lv_color_hex(0x8B95A3); }
inline lv_color_t accent() { return lv_color_hex(0x3EE8FF); }
inline lv_color_t rim() { return lv_color_hex(0x1F2630); }
inline lv_color_t warn() { return lv_color_hex(0xFBBF24); }

constexpr int kRimWidth = 4;   // ring along the edge of the round screen
constexpr int kSafeMargin = 8; // keep text this far inside the visible circle

} // namespace ui::theme
