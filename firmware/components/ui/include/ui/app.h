// The firmware's screens. M1: the eyes (tap = next mood, a demo until the MoodEngine
// exists) and an info screen with the touch tester; swipe left/right switches.
#pragma once

#include <cstdint>
#include <string_view>

#include "lvgl.h"
#include "ui/eyes.h"

namespace ui {

struct AppInfo {
    const char *version;
    const char *board;
    uint32_t seed; // randomness for blinks and glances
};

class App {
public:
    // create builds all screens and shows the eyes. Call with LVGL locked.
    static App *create(const AppInfo &info);

    Eyes *eyes() const { return eyes_; }
    lv_obj_t *eyes_screen() const { return eyes_screen_; }
    lv_obj_t *info_screen() const { return info_screen_; }

    // set_mood shows a mood on the eyes and in the label below them.
    void set_mood(std::string_view mood);

    // next_mood switches the eyes to the next preset (what a tap does).
    void next_mood();

private:
    static void on_eyes_event(lv_event_t *e);
    static void on_info_event(lv_event_t *e);
    void show(lv_obj_t *screen, lv_dir_t dir);

    lv_obj_t *eyes_screen_ = nullptr;
    lv_obj_t *info_screen_ = nullptr;
    lv_obj_t *mood_label_ = nullptr;
    Eyes *eyes_ = nullptr;
    size_t mood_index_ = 0;
    bool gesture_ = false; // the current touch became a swipe, so it is not a tap
};

} // namespace ui
