// M0 bring-up screen: rim ring, two eyes, title, board info and a touch tester.
#pragma once

#include "lvgl.h"

namespace ui {

struct HelloInfo {
    const char *version; // firmware version
    const char *board;   // board name, e.g. "ws146"
};

// create_hello builds the screen on parent (usually lv_screen_active()).
void create_hello(lv_obj_t *parent, const HelloInfo &info);

// touches returns how many taps the touch tester has counted.
int touches();

} // namespace ui
