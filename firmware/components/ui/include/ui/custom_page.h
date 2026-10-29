// CustomPageView shows one custom page from a host (core::CustomPages): its title and its
// widgets, laid out as in the emulator. The app has one such screen and fills it with the
// page that is shown. Also the icon names that notifications and pages use.
#pragma once

#include <string_view>
#include <utility>

#include "app_core/custom_pages.h"
#include "lvgl.h"

namespace ui {

// named_icon maps an icon name of the protocol ("ok", "ci_fail", "temp", …) to an icon and
// its colour; unknown names get a bell.
std::pair<const char *, lv_color_t> named_icon(std::string_view name);

class CustomPageView {
public:
    static CustomPageView *create(lv_obj_t *parent);
    // show draws page (nullptr: empty).
    void show(const core::CustomPage *page);

private:
    static void on_delete(lv_event_t *e);
    lv_obj_t *root_ = nullptr;
    std::vector<std::vector<lv_point_precise_t>> lines_; // the sparklines' points (LVGL keeps pointers)
};

} // namespace ui
