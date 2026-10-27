#include "ui/app.h"

#include "app_core/backlight.h"
#include "ui/fonts.h"
#include "ui/theme.h"

namespace ui {
namespace {

lv_obj_t *make_screen()
{
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s, theme::background(), 0);
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
    app->mood_label_ = lv_label_create(app->screens_[kEyes]);
    lv_obj_set_style_text_font(app->mood_label_, font::small(), 0);
    lv_obj_set_style_text_color(app->mood_label_, theme::muted(), 0);
    lv_obj_align(app->mood_label_, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_label_set_text(app->mood_label_, "neutral");

    app->dashboard_ = Dashboard::create(app->screens_[kDashboard], {info.store, info.settings, info.local_time});
    app->countdown_page_ = CountdownPage::create(app->screens_[kCountdown], app->countdown_, info.gestures);
    app->alarm_page_ = AlarmPage::create(app->screens_[kAlarm], app->alarms_, info.gestures);
    app->info_ = InfoPage::create(app->screens_[kInfo], {.version = info.version, .board = info.board,
                                                         .status = info.status});
    const StatusContext bar{info.store, info.settings, info.local_time, info.status};
    for (Screen s : {kEyes, kDashboard, kCountdown, kAlarm}) {
        app->bars_[s] = StatusBar::create(app->screens_[s], bar, s == kDashboard);
    }

    lv_screen_load(app->screens_[kEyes]);
    app->last_active_ = lv_tick_get();
    app->update_backlight(app->last_active_);
    app->timer_ = lv_timer_create(on_timer, 100, app);
    lv_obj_add_event_cb(app->screens_[kEyes], on_delete, LV_EVENT_DELETE, app);
    return app;
}

void App::set_mood(std::string_view mood)
{
    eyes_->set_mood(mood);
    lv_label_set_text(mood_label_, std::string(eyes_->mood()).c_str());
}

bool App::local(std::tm &t) const
{
    return info_in_.local_time && info_in_.local_time(t);
}

// update_mood shows what the MoodEngine decides, when that changes; the label says why.
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
    const std::string label = shown_.reason == "idle" ? shown_.name : shown_.name + "  ·  " + shown_.reason;
    lv_label_set_text(mood_label_, label.c_str());
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

void App::go(int step)
{
    current_ = (current_ + step + kScreens) % kScreens;
    if (bars_[current_] != nullptr) {
        bars_[current_]->refresh();
    }
    lv_screen_load_anim(screens_[current_], step > 0 ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                        250, 0, false);
}

void App::show(Screen s)
{
    current_ = s;
    if (bars_[s] != nullptr) {
        bars_[s]->refresh();
    }
    lv_screen_load(screens_[s]);
}

void App::input(core::Action a, float x)
{
    using core::Action;
    const uint32_t now = lv_tick_get();
    std::tm t{};
    const bool have_time = local(t);
    last_active_ = now;

    // A ringing alarm takes every input, on any page; a ringing countdown stops on a shake
    // from any page.
    if (alarms_.ringing()) {
        alarms_.input(a, x, current_ == kAlarm, t, now);
    } else if (countdown_.ringing() && a == Action::Shake) {
        countdown_.input(a, x, now);
    } else if (a == Action::SwipeLeft || a == Action::SwipeRight) {
        go(a == Action::SwipeLeft ? 1 : -1);
    } else if (current_ == kCountdown) {
        countdown_.input(a, x, now);
    } else if (current_ == kAlarm) {
        alarms_.input(a, x, true, have_time ? t : std::tm{}, now);
    } else if (current_ == kEyes && a == Action::Tap) {
        mood_.react("happy", "boop", 1500, now);
    } else if (a == Action::Shake) {
        mood_.react("dizzy", "shaken", 3000, now);
    }
    tick();
}

void App::tick()
{
    const uint32_t now = lv_tick_get();
    if (countdown_.tick(now) == core::Countdown::Event::Done) {
        show(kCountdown);
    }
    if (now - last_alarm_check_ >= 1000 || last_alarm_check_ == 0) {
        last_alarm_check_ = now;
        std::tm t{};
        if (alarms_.tick(local(t) ? &t : nullptr, now) == core::AlarmClock::Event::Ringing) {
            show(kAlarm);
        }
    }
    if (now - last_bar_ >= 1000) {
        last_bar_ = now;
        if (bars_[current_] != nullptr) {
            bars_[current_]->refresh();
        }
        host_lost_ = info_in_.status && info_in_.status().host_lost;
    }
    update_mood(now);
    update_backlight(now);
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

// on_event turns touches into actions. Until the IMU is used, a tap on the countdown page
// (or anywhere while it rings) stands in for a shake, and holding stops a ringing alarm.
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
            app->input(Action::SwipeLeft);
            break;
        case LV_DIR_RIGHT:
            app->input(Action::SwipeRight);
            break;
        case LV_DIR_TOP:
            app->input(Action::SwipeUp, app->press_x_);
            break;
        case LV_DIR_BOTTOM:
            app->input(Action::SwipeDown, app->press_x_);
            break;
        default:
            break;
        }
        break;
    }
    case LV_EVENT_SHORT_CLICKED:
        if (!app->gesture_) {
            const bool tap_is_shake = !app->info_in_.gestures.shake && !app->alarms_.ringing() &&
                                      (app->current_ == kCountdown || app->countdown_.ringing());
            app->input(tap_is_shake ? Action::Shake : Action::Tap);
        }
        break;
    case LV_EVENT_LONG_PRESSED:
        if (!app->gesture_) {
            const bool hold_is_shake = !app->info_in_.gestures.shake && app->alarms_.ringing();
            app->input(hold_is_shake ? Action::Shake : Action::LongPress);
        }
        break;
    default:
        break;
    }
}

} // namespace ui
