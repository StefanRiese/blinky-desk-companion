#include "ui/app.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "app_core/backlight.h"
#include "app_core/pages.h"
#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/round_layout.h"
#include "ui/theme.h"
#include "set.h"

namespace ui {
namespace {

constexpr uint32_t kIdleMs = 20000; // no touch for this long: the eyes look around

constexpr uint32_t kChimeMs = 400; // how long a notification's chime plays

// screen_for maps a ui.pages name to its screen; false for pages the firmware lacks.
bool screen_for(std::string_view page, App::Screen &s)
{
    static constexpr std::pair<std::string_view, App::Screen> kMap[] = {
        {"eyes", App::kEyes},         {"dashboard", App::kDashboard}, {"clock", App::kClock},
        {"forecast", App::kForecast}, {"indoor", App::kIndoor},       {"host", App::kHost},
        {"countdown", App::kCountdown}, {"alarm", App::kAlarm}};
    for (const auto &[name, screen] : kMap) {
        if (name == page) {
            s = screen;
            return true;
        }
    }
    return false;
}

lv_obj_t *make_screen()
{
    lv_obj_t *s = lv_obj_create(nullptr);
    set_bg_color(s, theme::background());
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s, false);
    return s;
}

} // namespace

App *App::create(const AppInfo &info)
{
    auto *app = new App(info);
    for (auto &s : app->screens_) {
        s = make_screen();
        lv_obj_add_event_cb(s, on_event, LV_EVENT_ALL, app);
    }

    app->eyes_ = Eyes::create(app->screens_[kEyes], info.seed);

    app->dashboard_ = Dashboard::create(app->screens_[kDashboard], {info.store, info.settings, info.local_time});
    app->clock_ = ClockPage::create(app->screens_[kClock], info.local_time);
    const DataPageContext data{info.store, info.settings};
    app->forecast_ = ForecastPage::create(app->screens_[kForecast], data);
    app->indoor_ = IndoorPage::create(app->screens_[kIndoor], data);
    app->host_ = HostDataPage::create(app->screens_[kHost], data);
    app->custom_view_ = CustomPageView::create(app->screens_[kCustom]);
    app->countdown_page_ = CountdownPage::create(app->screens_[kCountdown], app->countdown_);
    app->alarm_page_ = AlarmPage::create(app->screens_[kAlarm], app->alarms_);
    app->info_ = InfoPage::create(app->screens_[kInfo], {.version = info.version, .board = info.board,
                                                         .status = info.status});
    const StatusContext bar{info.store, info.settings, info.local_time, info.status, [app] { return app->timers(); }};
    for (Screen s : {kEyes, kDashboard, kForecast, kIndoor, kHost, kCountdown, kAlarm, kCustom}) {
        app->bars_[s] = StatusBar::create(app->screens_[s], bar, s == kDashboard);
    }

    // The notification box sits on the top layer, so it stays over every page.
    app->note_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(app->note_);
    lv_obj_set_size(app->note_, 288, 84);
    set_bg_color(app->note_, lv_color_hex(0x14181E));
    lv_obj_set_style_bg_opa(app->note_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(app->note_, theme::accent(), 0);
    lv_obj_set_style_border_width(app->note_, 2, 0);
    lv_obj_set_style_radius(app->note_, 18, 0);
    lv_obj_set_style_pad_hor(app->note_, 16, 0);
    lv_obj_set_flex_flow(app->note_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(app->note_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(app->note_, 12, 0);
    lv_obj_set_clickable(app->note_, false);
    place(app->note_, LV_ALIGN_TOP_MID, 0, 258);
    app->note_icon_ = lv_label_create(app->note_);
    lv_obj_set_style_text_font(app->note_icon_, font::icons(), 0);
    app->note_text_ = lv_label_create(app->note_);
    lv_obj_set_style_text_font(app->note_text_, font::body(), 0);
    set_text_color(app->note_text_, theme::text());
    lv_obj_set_width(app->note_text_, 212);
    lv_label_set_long_mode(app->note_text_, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_max_height(app->note_text_, 52, 0); // two lines; more is cut off
    lv_obj_set_hidden(app->note_, true);

    app->load_pages();
    app->load_color();
    app->current_ = app->home_;
    app->reported_page_ = app->page_name(); // the page it starts on is not a change
    lv_screen_load(app->screens_[app->home_]);
    app->last_active_ = lv_tick_get();
    app->update_backlight(app->last_active_);
    app->timer_ = lv_timer_create(on_timer, 100, app);
    lv_obj_add_event_cb(app->screens_[kEyes], on_delete, LV_EVENT_DELETE, app);
    return app;
}

void App::set_mood(std::string_view mood)
{
    eyes_->set_mood(mood);
}

bool App::local(std::tm &t) const
{
    return info_in_.local_time && info_in_.local_time(t);
}

// update_mood shows what the MoodEngine decides, when that changes.
void App::update_mood(uint32_t now)
{
    std::tm t{};
    const bool have_time = local(t);
    const mood::Context ctx{.alarm_ringing = alarms_.ringing(),
                            .countdown_ringing = countdown_.ringing(),
                            .local = have_time ? &t : nullptr,
                            .host_lost = host_lost_};
    mood::Mood m = mood_.evaluate(ctx, now);
    if (m == shown_) {
        return;
    }
    shown_ = std::move(m);
    eyes_->set_mood(shown_.name);
}

void App::update_backlight(uint32_t now)
{
    if (alarms_.ringing() || countdown_.ringing()) {
        last_active_ = now; // ringing keeps the screen awake
    }
    std::tm t{};
    const bool have_time = local(t);
    const int level = core::backlight(*info_in_.settings, have_time ? &t : nullptr, now - last_active_).level;
    if (level != backlight_) {
        backlight_ = level;
        if (info_in_.backlight) {
            info_in_.backlight(level);
        }
    }
}

void App::load_color()
{
    const std::string raw = info_in_.settings->get("ui.color");
    if (raw == color_raw_) {
        return;
    }
    color_raw_ = raw;
    const uint32_t rgb = raw.size() == 7 ? static_cast<uint32_t>(std::strtoul(raw.c_str() + 1, nullptr, 16)) : 0x3EE8FF;
    theme::accent_hex() = rgb;
    eyes_->set_color(rgb);
    set_border_color(note_, theme::accent());
    // The other pages pick up the accent when they refresh.
}

void App::load_pages()
{
    const std::string raw = info_in_.settings->get("ui.pages");
    if (raw == pages_raw_ && !order_.empty()) {
        return;
    }
    pages_raw_ = raw;
    const std::vector<std::string> names = core::pages(*info_in_.settings);
    order_.clear();
    for (const auto &n : names) {
        Screen s;
        if (screen_for(n, s)) {
            order_.push_back(s);
        }
    }
    if (order_.empty()) {
        order_.push_back(kEyes); // only pages the firmware doesn't have yet
    }
    Screen h;
    home_ = screen_for(core::home_page(names), h) ? h : order_.front();
}

void App::notify(const core::Notification &n)
{
    const uint32_t now = lv_tick_get();
    const auto [sym, color] = named_icon(n.icon);
    set_text(note_icon_, sym);
    set_text_color(note_icon_, color);
    set_text(note_text_, n.text.c_str());
    lv_obj_set_hidden(note_, false);
    note_at_ = now;
    note_until_ = now + static_cast<uint32_t>(n.dur) * 1000;
    note_sound_ = !n.sound.empty();
    if (!n.mood.empty()) {
        mood_.react(n.mood, "notification", static_cast<uint32_t>(n.dur) * 1000, now);
    }
    last_active_ = now; // wakes the backlight
    update_mood(now);
    update_backlight(now);
}

bool App::notifying() const
{
    return !lv_obj_is_hidden(note_);
}

core::Tune App::tune() const
{
    if (alarms_.ringing()) {
        return core::Tune::Alarm;
    }
    if (countdown_.ringing()) {
        return core::Tune::Countdown;
    }
    if (note_sound_ && notifying() && lv_tick_get() - note_at_ < kChimeMs) {
        return core::Tune::Notify;
    }
    return core::Tune::None;
}

Timers App::timers() const
{
    Timers t;
    const auto v = countdown_.view(lv_tick_get());
    if ((v.running || v.ringing) && current_ != kCountdown) {
        const uint32_t s = (v.left_ms + 999) / 1000;
        char buf[16];
        std::snprintf(buf, sizeof buf, "%02u:%02u", static_cast<unsigned>(s / 60), static_cast<unsigned>(s % 60));
        t.countdown = buf;
    }
    if (current_ != kAlarm) {
        for (const auto &a : alarms_.alarms()) {
            t.alarm = t.alarm || a.on;
        }
    }
    return t;
}

std::vector<App::Slot> App::slots() const
{
    std::vector<Slot> out;
    for (Screen s : order_) {
        out.push_back({s, ""});
    }
    auto shown = [&](Screen s) {
        return std::find_if(out.begin(), out.end(), [s](const Slot &x) { return x.s == s; }) != out.end();
    };
    if (current_ == kCountdown && !shown(kCountdown) && (countdown_.running() || countdown_.ringing())) {
        out.push_back({kCountdown, ""});
    }
    if (current_ == kAlarm && !shown(kAlarm) && alarms_.ringing()) {
        out.push_back({kAlarm, ""});
    }
    for (const std::string &name : custom_.names()) {
        out.push_back({kCustom, name});
    }
    out.push_back({kInfo, ""});
    return out;
}

std::vector<App::Screen> App::pages() const
{
    std::vector<Screen> out;
    for (const Slot &s : slots()) {
        out.push_back(s.s);
    }
    return out;
}

int App::slot_index(const std::vector<Slot> &list) const
{
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i].s == current_ && (current_ != kCustom || list[i].custom == custom_shown_)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void App::go(int step)
{
    const std::vector<Slot> list = slots();
    const int n = static_cast<int>(list.size());
    const int at = slot_index(list);
    const int i = at < 0 ? (step > 0 ? -1 : 0) : at;
    const Slot &next = list[static_cast<size_t>((i + step + n) % n)];
    const bool same_screen = current_ == kCustom && next.s == kCustom; // one custom page to the next
    current_ = next.s;
    if (next.s == kCustom) {
        custom_shown_ = next.custom;
        custom_view_->show(custom_.get(custom_shown_));
        custom_version_ = custom_.version();
    }
    if (bars_[current_] != nullptr) {
        bars_[current_]->refresh();
    }
    if (!same_screen) {
        lv_screen_load_anim(screens_[current_], step > 0 ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                            250, 0, false);
    }
}

void App::emit(std::string_view kind, std::string_view name, std::string_view state)
{
    if (info_in_.event) {
        info_in_.event(kind, name, state);
    }
}

std::string App::page_name() const
{
    static constexpr std::pair<Screen, const char *> kNames[] = {
        {kEyes, "eyes"},       {kDashboard, "dashboard"}, {kClock, "clock"},     {kForecast, "forecast"},
        {kIndoor, "indoor"},   {kHost, "host"},           {kCountdown, "countdown"}, {kAlarm, "alarm"},
        {kInfo, "info"}};
    if (current_ == kCustom) {
        return custom_shown_;
    }
    for (const auto &[s, name] : kNames) {
        if (s == current_) {
            return name;
        }
    }
    return "";
}

namespace {

const char *countdown_state(core::Countdown::Event e)
{
    using E = core::Countdown::Event;
    switch (e) {
    case E::Started:
        return "started";
    case E::Paused:
        return "paused";
    case E::Reset:
        return "reset";
    case E::Done:
        return "done";
    case E::Stopped:
        return "stopped";
    case E::None:
        break;
    }
    return nullptr;
}

const char *alarm_state(core::AlarmClock::Event e)
{
    using E = core::AlarmClock::Event;
    switch (e) {
    case E::Ringing:
        return "ringing";
    case E::Snoozed:
        return "snoozed";
    case E::Stopped:
        return "stopped";
    case E::None:
        break;
    }
    return nullptr;
}

} // namespace

void App::gesture(core::Gesture g)
{
    if (g == core::Gesture::None) {
        return;
    }
    emit("gesture", core::gesture_name(g));
    switch (g) {
    case core::Gesture::Shake: {
        // A shake only makes the eyes dizzy (and hides a notification): the countdown and
        // the alarms run on taps and holds. A ringing alarm ignores it.
        if (alarms_.ringing()) {
            break;
        }
        const uint32_t now = lv_tick_get();
        last_active_ = now;
        if (notifying()) {
            lv_obj_set_hidden(note_, true);
        }
        mood_.react("dizzy", "shaken", 3000, now);
        tick();
        break;
    }
    case core::Gesture::FlickRight:
        input(core::Action::SwipeLeft); // as a swipe to the left: the next page
        break;
    case core::Gesture::FlickLeft:
        input(core::Action::SwipeRight);
        break;
    case core::Gesture::None:
        break;
    }
}

void App::show_custom(const std::string &name)
{
    if (custom_.get(name) == nullptr) {
        return;
    }
    custom_shown_ = name;
    custom_view_->show(custom_.get(name));
    custom_version_ = custom_.version();
    show(kCustom);
}

void App::show(Screen s)
{
    current_ = s;
    if (bars_[s] != nullptr) {
        bars_[s]->refresh();
    }
    lv_screen_load(screens_[s]);
}

void App::go_home()
{
    if (current_ == home_) {
        return;
    }
    current_ = home_;
    if (bars_[current_] != nullptr) {
        bars_[current_]->refresh();
    }
    lv_screen_load_anim(screens_[current_], LV_SCREEN_LOAD_ANIM_MOVE_TOP, 250, 0, false);
}

// home_due reports whether ui.home_after seconds passed without activity (0 = never). A
// ringing alarm and a running or ringing countdown on its page keep the page.
bool App::home_due(uint32_t now) const
{
    const double after = info_in_.settings->number("ui.home_after");
    if (after <= 0 || current_ == home_ || now - last_active_ < after * 1000 || alarms_.ringing()) {
        return false;
    }
    return !(current_ == kCountdown && (countdown_.running() || countdown_.ringing()));
}

void App::input(core::Action a, float x)
{
    using core::Action;
    const uint32_t now = lv_tick_get();
    std::tm t{};
    const bool have_time = local(t);
    last_active_ = now;

    // A ringing alarm takes every input, on any page; a ringing countdown stops on a tap
    // from any page (on_event turns those taps into Action::Shake, the models' start/stop).
    // A tap on a notification hides it; a start/stop tap hides it and still acts.
    if (notifying() && (a == Action::Tap || a == Action::Shake) && !alarms_.ringing()) {
        lv_obj_set_hidden(note_, true);
        if (a == Action::Tap) {
            tick();
            return;
        }
    }
    core::Countdown::Event cd = core::Countdown::Event::None;
    core::AlarmClock::Event al = core::AlarmClock::Event::None;
    const std::string ring_at = alarms_.ring_at();
    if (alarms_.ringing()) {
        alarms_.input(a, x, current_ == kAlarm, t, now, &al);
    } else if (countdown_.ringing() && a == Action::Shake) {
        countdown_.input(a, x, now, &cd);
    } else if (a == Action::SwipeLeft || a == Action::SwipeRight) {
        go(a == Action::SwipeLeft ? 1 : -1);
    } else if (current_ == kCountdown) {
        countdown_.input(a, x, now, &cd);
    } else if (current_ == kAlarm) {
        alarms_.input(a, x, true, have_time ? t : std::tm{}, now, &al);
    } else if (a == Action::SwipeUp) {
        go_home();
    } else if (current_ == kEyes && a == Action::Tap) {
        mood_.react("happy", "boop", 1500, now);
    } else if (a == Action::Shake) {
        mood_.react("dizzy", "shaken", 3000, now);
    }
    if (const char *s = countdown_state(cd)) {
        emit("countdown", "", s);
    }
    if (const char *s = alarm_state(al)) {
        emit("alarm", ring_at, s);
    }
    tick();
}

void App::tick()
{
    const uint32_t now = lv_tick_get();
    const core::Countdown::Event cd = countdown_.tick(now);
    if (cd == core::Countdown::Event::Done) {
        show(kCountdown);
    }
    if (const char *s = countdown_state(cd)) {
        emit("countdown", "", s);
    }
    if (now - last_alarm_check_ >= 1000 || last_alarm_check_ == 0) {
        last_alarm_check_ = now;
        std::tm t{};
        const core::AlarmClock::Event al = alarms_.tick(local(t) ? &t : nullptr, now);
        if (al == core::AlarmClock::Event::Ringing) {
            show(kAlarm);
        }
        if (const char *s = alarm_state(al)) {
            emit("alarm", alarms_.ring_at(), s);
        }
    }
    load_pages(); // ui.pages may have changed (host, setup page, web page)
    load_color(); // and ui.color
    custom_.expire(now);
    if (slot_index(slots()) < 0) {
        show(home_); // the page shown was hidden, its countdown ended, or a host removed it
    } else if (current_ == kCustom && custom_.version() != custom_version_) {
        custom_view_->show(custom_.get(custom_shown_)); // the host sent it again
        custom_version_ = custom_.version();
    }
    if (home_due(now)) {
        go_home(); // nobody touched it for ui.home_after seconds
    }
    if (const std::string page = page_name(); page != reported_page_) {
        emit("page", page);
        reported_page_ = page;
    }
    if (now - last_bar_ >= 1000) {
        last_bar_ = now;
        if (bars_[current_] != nullptr) {
            bars_[current_]->refresh();
        }
        host_lost_ = info_in_.status && info_in_.status().host_lost;
    }
    if (notifying() && static_cast<int32_t>(now - note_until_) >= 0) {
        lv_obj_set_hidden(note_, true);
    }
    update_mood(now);
    update_backlight(now);
    eyes_->set_idle(now - last_active_ >= kIdleMs);
    if (current_ == kCountdown) {
        countdown_page_->refresh(now);
    } else if (current_ == kAlarm) {
        std::tm t{};
        alarm_page_->refresh(now, local(t) ? &t : nullptr);
    }
}

void App::on_timer(lv_timer_t *t)
{
    static_cast<App *>(lv_timer_get_user_data(t))->tick();
}

// on_delete stops the timer when the screens go away (the display was deleted).
void App::on_delete(lv_event_t *e)
{
    auto *app = static_cast<App *>(lv_event_get_user_data(e));
    lv_timer_delete(app->timer_);
    app->timer_ = nullptr;
    app->bars_.fill(nullptr);
}

// on_event turns touches into actions. A tap on the countdown page (or anywhere while it
// rings) and holding a ringing alarm are Action::Shake, the models' start/stop action.
void App::on_event(lv_event_t *e)
{
    using core::Action;
    auto *app = static_cast<App *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED: {
        app->gesture_ = false;
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);
        app->press_x_ = static_cast<float>(p.x) / static_cast<float>(lv_obj_get_width(app->screens_[0]));
        break;
    }
    case LV_EVENT_GESTURE: {
        app->gesture_ = true;
        switch (lv_indev_get_gesture_dir(lv_indev_active())) {
        case LV_DIR_LEFT:
            app->emit("touch", "swipe_left");
            app->input(Action::SwipeLeft);
            break;
        case LV_DIR_RIGHT:
            app->emit("touch", "swipe_right");
            app->input(Action::SwipeRight);
            break;
        case LV_DIR_TOP:
            app->emit("touch", "swipe_up");
            app->input(Action::SwipeUp, app->press_x_);
            break;
        case LV_DIR_BOTTOM:
            app->emit("touch", "swipe_down");
            app->input(Action::SwipeDown, app->press_x_);
            break;
        default:
            break;
        }
        break;
    }
    case LV_EVENT_SHORT_CLICKED:
        if (!app->gesture_) {
            const bool tap_is_shake =
                !app->alarms_.ringing() && (app->current_ == kCountdown || app->countdown_.ringing());
            app->emit("touch", "tap");
            app->input(tap_is_shake ? Action::Shake : Action::Tap);
        }
        break;
    case LV_EVENT_LONG_PRESSED:
        if (!app->gesture_) {
            const bool hold_is_shake = app->alarms_.ringing();
            app->emit("touch", "long_press");
            app->input(hold_is_shake ? Action::Shake : Action::LongPress);
        }
        break;
    default:
        break;
    }
}

} // namespace ui
