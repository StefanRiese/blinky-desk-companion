// Dashboard page: clock, date, current weather and indoor climate (as in the emulator).
// Reads the DataStore and the settings once a second; labels are bound to LVGL subjects,
// so a label redraws only when its text changes.
#pragma once

#include <array>
#include <ctime>
#include <functional>

#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "lvgl.h"

namespace ui {

// LocalTime fills tm with the local time; returns false while the clock is not set.
using LocalTime = std::function<bool(std::tm &)>;

struct DashboardContext {
    const core::DataStore *store;
    const core::Settings *settings;
    LocalTime local_time;
};

// weather_icon and weather_label describe a WMO weather code (as used by Open-Meteo).
const char *weather_icon(int code, bool day);
const char *weather_label(int code);

class Dashboard {
public:
    static Dashboard *create(lv_obj_t *parent, const DashboardContext &ctx);
    // refresh updates all values now (also runs once a second by itself).
    void refresh();

private:
    explicit Dashboard(const DashboardContext &ctx) : ctx_(ctx) {}
    static void on_timer(lv_timer_t *t);
    static void on_delete(lv_event_t *e);

    DashboardContext ctx_;
    lv_timer_t *timer_ = nullptr;
    lv_obj_t *weather_row_ = nullptr, *no_weather_ = nullptr, *rain_row_ = nullptr, *indoor_row_ = nullptr;
    // One string subject per changing text (bound labels redraw when it changes), with its
    // current and previous value buffers.
    struct Text {
        lv_subject_t *sub = nullptr;
        char buf[64] = "", prev[64] = "";
    };
    Text clock_, date_, w_icon_, w_temp_, w_line_, in_temp_, in_hum_, rain_;
    std::array<Text *, 8> texts() { return {&clock_, &date_, &w_icon_, &w_temp_, &w_line_, &in_temp_, &in_hum_, &rain_}; }
};

} // namespace ui
