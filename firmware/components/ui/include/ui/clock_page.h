// Analog clock page: a dial with 12 hour marks, hour, minute and second hands and the date
// below the centre (as in the emulator). The hands move once a second; without the time
// they are hidden. The face fills the round screen, so the page has no status bar.
#pragma once

#include <array>
#include <ctime>
#include <utility>

#include "lvgl.h"
#include "ui/dashboard.h"

namespace ui {

class ClockPage {
public:
    static ClockPage *create(lv_obj_t *parent, LocalTime local_time);
    // refresh moves the hands to the current time (also runs once a second by itself).
    void refresh();

private:
    explicit ClockPage(LocalTime local_time) : local_time_(std::move(local_time)) {}
    static void on_timer(lv_timer_t *t);
    static void on_delete(lv_event_t *e);

    struct Hand {
        lv_obj_t *line = nullptr;
        std::array<lv_point_precise_t, 2> points{};
    };
    LocalTime local_time_;
    lv_timer_t *timer_ = nullptr;
    Hand hour_, minute_, second_;
    lv_obj_t *centre_ = nullptr, *date_ = nullptr;
};

} // namespace ui
