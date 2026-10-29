// The firmware's screens: eyes, dashboard, analog clock, forecast, indoor trend, host data,
// countdown, alarm and an info screen
// (links, sources, touch tester). ui.pages picks and orders the pages; swipe left/right moves
// through them in a circle, with the info screen always last, as in the emulator. Swipe up
// goes home (the eyes, or the first page if they are hidden). A finished countdown or a
// ringing alarm jumps to its page, even when it is hidden. Every page but the clock and the
// info screen has a status bar at the top.
//
// The MoodEngine picks the eyes' mood (a tap on the eyes is a "boop": happy for a moment; a
// shake makes them dizzy). The backlight follows the time of day and dims when nobody
// touches the screen; touches and ringing wake it.
#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <functional>
#include <string>
#include <string_view>

#include "app_core/alarm_clock.h"
#include "app_core/countdown.h"
#include "app_core/data_store.h"
#include "app_core/host_link.h"
#include "app_core/sound.h"
#include "app_core/settings.h"
#include "lvgl.h"
#include "mood/mood_engine.h"
#include "ui/clock_page.h"
#include "ui/dashboard.h"
#include "ui/data_pages.h"
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
    enum Screen { kEyes, kDashboard, kClock, kForecast, kIndoor, kHost, kCountdown, kAlarm, kInfo, kScreens };

    // create builds all screens and shows the eyes. Call with LVGL locked.
    static App *create(const AppInfo &info);

    Eyes *eyes() const { return eyes_; }
    Dashboard *dashboard() const { return dashboard_; }
    ClockPage *clock() const { return clock_; }
    ForecastPage *forecast() const { return forecast_; }
    IndoorPage *indoor() const { return indoor_; }
    HostDataPage *host_data() const { return host_; }
    InfoPage *info() const { return info_; }
    lv_obj_t *screen(Screen s) const { return screens_[s]; }
    Screen current() const { return static_cast<Screen>(current_); }
    core::Countdown &countdown() { return countdown_; }
    core::AlarmClock &alarms() { return alarms_; }
    mood::MoodEngine &mood() { return mood_; }
    const mood::Mood &shown_mood() const { return shown_; }
    int backlight() const { return backlight_; }

    // set_mood shows a mood on the eyes until the MoodEngine decides on another one
    // (screenshots, tests).
    void set_mood(std::string_view mood);

    // input handles an action from touch, buttons or the IMU; x is where a swipe started
    // (0…1 across the screen, < 0 unknown).
    void input(core::Action a, float x = -1);

    // tick advances timers and alarms, updates the mood, the backlight and the idle eyes, redraws the
    // visible timer page and, once a second, the visible status bar (runs every 100 ms).
    void tick();

    // show switches to a screen without animation (used when something rings, and by tests).
    void show(Screen s);

    // pages returns the screens to swipe through now: ui.pages, a hidden countdown or alarm
    // page while it is on screen and running or ringing, then the info screen.
    std::vector<Screen> pages() const;
    // notify shows a host's notification over every page for its duration: icon and text,
    // its mood on the eyes meanwhile, a chime if it has a sound. A tap or a shake hides it.
    void notify(const core::Notification &n);
    bool notifying() const;
    // tune is what the speaker plays now: a ringing alarm or countdown, or a notification's
    // chime right after it came.
    core::Tune tune() const;

    // timers is what the status bar shows of the countdown and the alarms.
    Timers timers() const;
    // home is where swipe up goes and where the firmware starts.
    Screen home() const { return home_; }

private:
    App(const AppInfo &info) : alarms_(*info.settings), mood_(*info.settings, *info.store), info_in_(info) {}
    static void on_event(lv_event_t *e);
    static void on_timer(lv_timer_t *t);
    static void on_delete(lv_event_t *e);
    void go(int step);      // +1 next screen (swipe left), -1 previous
    void load_pages();      // reads ui.pages (again when it changed)
    void update_mood(uint32_t now);
    void update_backlight(uint32_t now);
    bool local(std::tm &t) const;

    std::array<lv_obj_t *, kScreens> screens_{};
    int current_ = kEyes;
    std::vector<Screen> order_; // the shown pages from ui.pages, without the info screen
    Screen home_ = kEyes;
    std::string pages_raw_;     // ui.pages as last read
    lv_timer_t *timer_ = nullptr;
    Eyes *eyes_ = nullptr;
    Dashboard *dashboard_ = nullptr;
    ClockPage *clock_ = nullptr;
    ForecastPage *forecast_ = nullptr;
    IndoorPage *indoor_ = nullptr;
    HostDataPage *host_ = nullptr;
    CountdownPage *countdown_page_ = nullptr;
    AlarmPage *alarm_page_ = nullptr;
    InfoPage *info_ = nullptr;
    std::array<StatusBar *, kScreens> bars_{}; // none on the clock and the info screen
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
    lv_obj_t *note_ = nullptr, *note_icon_ = nullptr, *note_text_ = nullptr; // the notification
    uint32_t note_at_ = 0, note_until_ = 0;
    bool note_sound_ = false;
    uint32_t last_bar_ = 0;
};

} // namespace ui
