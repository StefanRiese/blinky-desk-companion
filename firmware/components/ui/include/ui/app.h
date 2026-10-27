// The firmware's screens: eyes, dashboard, countdown, alarm and an info screen (links,
// sources, touch tester). Swipe left/right moves through them in a circle, as in the
// emulator. A finished countdown or a ringing alarm jumps to its page. Every page but the
// info screen has a status bar at the top.
//
// The MoodEngine picks the eyes' mood (a tap on the eyes is a "boop": happy for a moment; a
// shake makes them dizzy). The backlight follows the time of day and dims when nobody
// touches the screen; touches and ringing wake it.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "app_core/alarm_clock.h"
#include "app_core/countdown.h"
#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "lvgl.h"
#include "mood/mood_engine.h"
#include "ui/dashboard.h"
#include "ui/eyes.h"
#include "ui/info.h"
#include "ui/status_bar.h"
#include "ui/timer_pages.h"

namespace ui {

struct AppInfo {
    const char *version;
    const char *board;
    uint32_t seed; // randomness for blinks and glances
    const core::DataStore *store;
    core::Settings *settings; // the alarm page saves alarm.list
    LocalTime local_time;
    Gestures gestures; // which gestures the board has (shake needs the IMU)
    StatusFn status;   // WiFi, links and sources for the status bars and the info screen
    std::function<void(int level)> backlight; // 0 … 255; called when the level changes
};

class App {
public:
    enum Screen { kEyes, kDashboard, kCountdown, kAlarm, kInfo, kScreens };

    // create builds all screens and shows the eyes. Call with LVGL locked.
    static App *create(const AppInfo &info);

    Eyes *eyes() const { return eyes_; }
    Dashboard *dashboard() const { return dashboard_; }
    InfoPage *info() const { return info_; }
    lv_obj_t *screen(Screen s) const { return screens_[s]; }
    Screen current() const { return static_cast<Screen>(current_); }
    core::Countdown &countdown() { return countdown_; }
    core::AlarmClock &alarms() { return alarms_; }
    mood::MoodEngine &mood() { return mood_; }
    const mood::Mood &shown_mood() const { return shown_; }
    int backlight() const { return backlight_; }

    // set_mood shows a mood on the eyes and in the label below them, until the MoodEngine
    // decides on another one (screenshots, tests).
    void set_mood(std::string_view mood);

    // input handles an action from touch, buttons or the IMU; x is where a swipe started
    // (0…1 across the screen, < 0 unknown).
    void input(core::Action a, float x = -1);

    // tick advances timers and alarms, updates the mood and the backlight, redraws the
    // visible timer page and, once a second, the visible status bar (runs every 100 ms).
    void tick();

    // show switches to a screen without animation (used when something rings, and by tests).
    void show(Screen s);

private:
    App(const AppInfo &info) : alarms_(*info.settings), mood_(*info.settings, *info.store), info_in_(info) {}
    static void on_event(lv_event_t *e);
    static void on_timer(lv_timer_t *t);
    static void on_delete(lv_event_t *e);
    void go(int step);      // +1 next screen (swipe left), -1 previous
    void update_mood(uint32_t now);
    void update_backlight(uint32_t now);
    bool local(std::tm &t) const;

    std::array<lv_obj_t *, kScreens> screens_{};
    int current_ = kEyes;
    lv_timer_t *timer_ = nullptr;
    lv_obj_t *mood_label_ = nullptr;
    Eyes *eyes_ = nullptr;
    Dashboard *dashboard_ = nullptr;
    CountdownPage *countdown_page_ = nullptr;
    AlarmPage *alarm_page_ = nullptr;
    InfoPage *info_ = nullptr;
    std::array<StatusBar *, kScreens> bars_{}; // none on the info screen
    core::Countdown countdown_;
    core::AlarmClock alarms_;
    mood::MoodEngine mood_;
    AppInfo info_in_;
    mood::Mood shown_;      // what the engine decided last
    bool host_lost_ = false; // from the status, once a second
    uint32_t last_active_ = 0;
    int backlight_ = -1;
    bool gesture_ = false; // the current touch became a swipe, so it is not a tap
    float press_x_ = -1;   // where the current touch started (0…1)
    uint32_t last_alarm_check_ = 0;
    uint32_t last_bar_ = 0;
};

} // namespace ui
