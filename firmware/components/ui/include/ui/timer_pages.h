// Countdown and alarm pages (as in the emulator). They only draw; the App feeds them the
// models (core::Countdown, core::AlarmClock) and routes input to the models.
#pragma once

#include <array>
#include <cstdint>
#include <ctime>

#include "app_core/alarm_clock.h"
#include "app_core/countdown.h"
#include "lvgl.h"

namespace ui {

// shake_word is how hints name the start/stop gesture: "shake" once the IMU works, "tap"
// (and "hold" to stop an alarm) until then.
struct Gestures {
    bool shake = false;
};

class CountdownPage {
public:
    static CountdownPage *create(lv_obj_t *parent, const core::Countdown &model, Gestures g);
    void refresh(uint32_t now_ms);

private:
    CountdownPage(const core::Countdown &m, Gestures g) : model_(m), g_(g) {}
    const core::Countdown &model_;
    Gestures g_;
    lv_obj_t *ring_ = nullptr, *mm_ = nullptr, *colon_ = nullptr, *ss_ = nullptr, *hint_ = nullptr;
    lv_obj_t *edit_ = nullptr; // arrows and min/sec labels, shown while editable
};

class AlarmPage {
public:
    static AlarmPage *create(lv_obj_t *parent, const core::AlarmClock &model, Gestures g);
    // refresh redraws for now; local is nullptr while the clock is not set.
    void refresh(uint32_t now_ms, const std::tm *local);

private:
    AlarmPage(const core::AlarmClock &m, Gestures g) : model_(m), g_(g) {}
    const core::AlarmClock &model_;
    Gestures g_;
    lv_obj_t *normal_ = nullptr, *ringing_ = nullptr, *empty_ = nullptr;
    lv_obj_t *hh_ = nullptr, *colon_ = nullptr, *mm_ = nullptr, *badge_ = nullptr, *badge_text_ = nullptr;
    lv_obj_t *info_ = nullptr, *ring_time_ = nullptr, *ring_hint_ = nullptr;
    std::array<lv_obj_t *, 7> days_{};
};

} // namespace ui
