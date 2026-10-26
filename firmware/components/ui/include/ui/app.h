// The firmware's screens: eyes (tap = next mood, a demo until the MoodEngine exists),
// dashboard (clock, weather, indoor) and an info screen with the touch tester.
// Swipe left/right moves through them in a circle, as in the emulator.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "lvgl.h"
#include "ui/dashboard.h"
#include "ui/eyes.h"

namespace ui {

struct AppInfo {
    const char *version;
    const char *board;
    uint32_t seed; // randomness for blinks and glances
    const core::DataStore *store;
    const core::Settings *settings;
    LocalTime local_time;
};

class App {
public:
    enum Screen { kEyes, kDashboard, kInfo, kScreens };

    // create builds all screens and shows the eyes. Call with LVGL locked.
    static App *create(const AppInfo &info);

    Eyes *eyes() const { return eyes_; }
    Dashboard *dashboard() const { return dashboard_; }
    lv_obj_t *screen(Screen s) const { return screens_[s]; }

    // set_mood shows a mood on the eyes and in the label below them.
    void set_mood(std::string_view mood);
    // next_mood switches the eyes to the next preset (what a tap does).
    void next_mood();

private:
    static void on_event(lv_event_t *e);
    void go(int step); // +1 next screen (swipe left), -1 previous

    std::array<lv_obj_t *, kScreens> screens_{};
    int current_ = kEyes;
    lv_obj_t *mood_label_ = nullptr;
    Eyes *eyes_ = nullptr;
    Dashboard *dashboard_ = nullptr;
    size_t mood_index_ = 0;
    bool gesture_ = false; // the current touch became a swipe, so it is not a tap
};

} // namespace ui
