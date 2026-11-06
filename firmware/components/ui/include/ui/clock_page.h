// Analog clock page: a dial with 12 hour marks, hour, minute and second hands and the date
// below the centre (as in the emulator). The App refreshes it at every tick while shown, so
// the second hand moves right on the second; without the time the hands are hidden. The face fills the round screen, so the page has no status bar.
#pragma once

#include <array>
#include <ctime>
#include <utility>

#include "lvgl.h"
#include "ui/dashboard.h"
#include "ui/page.h"

namespace ui {

class ClockPage : public Page {
public:
    static ClockPage *create(lv_obj_t *parent, LocalTime local_time);
    // refresh moves the hands to the current time; nothing redraws if they stay.
    void refresh() override;

private:
    explicit ClockPage(LocalTime local_time) : local_time_(std::move(local_time)) {}
    static void on_delete(lv_event_t *e);

    struct Hand {
        lv_obj_t *line = nullptr;
        std::array<lv_point_precise_t, 2> points{}; // relative to the line object
        std::array<lv_point_precise_t, 2> at{{{-1, -1}, {-1, -1}}}; // on the face
        void move(lv_point_precise_t a, lv_point_precise_t b);
    };
    LocalTime local_time_;
    Hand hour_, minute_, second_;
    lv_obj_t *centre_ = nullptr, *date_ = nullptr;
};

} // namespace ui
