// The firmware's screens: eyes, dashboard, analog clock, forecast, indoor trend, host data,
// countdown, alarm and an info screen
// (links, sources, touch tester). ui.pages picks and orders the pages; swipe left/right moves
// through them in a circle, with the info screen always last, as in the emulator. Swipe up
// goes home (the eyes, or the first page if they are hidden), and so does ui.home_after seconds
// without a touch (0 = never; a running countdown keeps its page). A finished countdown or a
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
#include "app_core/custom_pages.h"
#include "app_core/data_store.h"
#include "app_core/host_link.h"
#include "app_core/motion.h"
#include "app_core/sound.h"
#include "app_core/settings.h"
#include "lvgl.h"
#include "mood/mood_engine.h"
#include "ui/clock_page.h"
#include "ui/custom_page.h"
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
    StatusFn status;   // WiFi, links and sources for the status bars and the info screen
    std::function<void(int level)> backlight; // 0 … 255; called when the level changes
    // event reports a device event to the hosts: touch, page, countdown, alarm (as the
    // emulator, protocol "event" messages). May be empty.
    std::function<void(std::string_view kind, std::string_view name, std::string_view state)> event;
};

class App {
public:
    // kCustom shows whichever custom page from a host is current.
    enum Screen { kEyes, kDashboard, kClock, kForecast, kIndoor, kHost, kCountdown, kAlarm, kCustom, kInfo, kScreens };

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

    // gesture handles a motion gesture from the IMU: a shake makes the eyes dizzy (it never
    // starts or stops the countdown or an alarm), a flick to the right turns to the next
    // page, to the left to the previous one. It is reported to the hosts too.
    void gesture(core::Gesture g);

    // input handles an action from touch, buttons or the IMU; x is where a swipe started
    // (0…1 across the screen, < 0 unknown).
    void input(core::Action a, float x = -1);

    // tick advances timers and alarms, updates the mood, the backlight and the idle eyes, redraws the
    // visible timer page and, once a second, the visible status bar (runs every 100 ms).
    void tick();

    // show switches to a screen without animation (used when something rings, and by tests).
    void show(Screen s);

    // pages returns the screens to swipe through now: ui.pages, a hidden countdown or alarm
    // page while it is on screen and running or ringing, the custom pages from hosts
    // (kCustom once per page, sorted by name), then the info screen.
    std::vector<Screen> pages() const;

    // custom_pages holds the pages hosts sent ("page" messages); show_custom shows one.
    core::CustomPages &custom_pages() { return custom_; }
    void show_custom(const std::string &name);
    const std::string &custom_shown() const { return custom_shown_; }
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
    void go_home();         // to home_, sliding up (swipe up, ui.home_after)
    bool home_due(uint32_t now) const;
    void emit(std::string_view kind, std::string_view name, std::string_view state = {});
    std::string page_name() const; // the page shown, as the hosts know it
    void load_pages();      // reads ui.pages (again when it changed)
    void load_color();      // reads ui.color (again when it changed): eyes and accent
    struct Slot {
        Screen s;
        std::string custom; // the custom page's name for kCustom
    };
    std::vector<Slot> slots() const; // pages(), with the custom pages' names
    int slot_index(const std::vector<Slot> &list) const;
    void update_mood(uint32_t now);
    void update_backlight(uint32_t now);
    bool local(std::tm &t) const;

    std::array<lv_obj_t *, kScreens> screens_{};
    int current_ = kEyes;
    std::vector<Screen> order_; // the shown pages from ui.pages, without the info screen
    Screen home_ = kEyes;
    std::string pages_raw_;     // ui.pages as last read
    std::string color_raw_;     // ui.color as last read
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
    core::CustomPages custom_;
    CustomPageView *custom_view_ = nullptr;
    std::string custom_shown_;
    uint32_t custom_version_ = 0;
    std::string reported_page_; // the page last reported to the hosts
    lv_obj_t *note_ = nullptr, *note_icon_ = nullptr, *note_text_ = nullptr; // the notification
    uint32_t note_at_ = 0, note_until_ = 0;
    bool note_sound_ = false;
    uint32_t last_bar_ = 0;
};

} // namespace ui
