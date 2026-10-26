#include "app_core/countdown.h"

namespace core {
namespace {

uint32_t ceil_sec(uint32_t ms)
{
    return (ms + 999) / 1000 * 1000;
}

} // namespace

bool Countdown::input(Action a, float x, uint32_t now, Event *ev)
{
    Event e = Event::None;
    bool used = true;
    switch (a) {
    case Action::SwipeUp:
    case Action::SwipeDown: {
        if (running_ || ringing_) {
            break; // no changes while it runs or rings
        }
        const int step = a == Action::SwipeUp ? 1 : -1;
        const uint32_t cur = ceil_sec(left_);
        int m = static_cast<int>(cur / 60000), s = static_cast<int>(cur % 60000 / 1000);
        if (x >= 0.5f) {
            s = (s + step + 60) % 60;
        } else {
            m = (m + step + kMaxMinutes + 1) % (kMaxMinutes + 1);
        }
        left_ = set_ = static_cast<uint32_t>(m * 60000 + s * 1000);
        break;
    }
    case Action::Shake:
        if (ringing_) {
            ringing_ = false;
            left_ = set_;
            e = Event::Stopped;
        } else if (running_) {
            running_ = false;
            left_ = static_cast<int32_t>(end_ - now) > 0 ? end_ - now : 0;
            e = Event::Paused;
        } else if (left_ > 0) {
            running_ = true;
            end_ = now + left_;
            e = Event::Started;
        } else {
            used = false; // nothing set: a normal shake
        }
        break;
    case Action::LongPress:
        running_ = ringing_ = false;
        left_ = set_;
        e = Event::Reset;
        break;
    default:
        used = false;
    }
    if (ev != nullptr) {
        *ev = e;
    }
    return used;
}

Countdown::Event Countdown::tick(uint32_t now)
{
    if (running_ && static_cast<int32_t>(now - end_) >= 0) {
        running_ = false;
        left_ = 0;
        ringing_ = true;
        ring_start_ = now;
        return Event::Done;
    }
    if (ringing_ && static_cast<int32_t>(now - ring_start_) >= static_cast<int32_t>(kRingMs)) {
        ringing_ = false;
        left_ = set_;
        return Event::Stopped;
    }
    return Event::None;
}

Countdown::View Countdown::view(uint32_t now) const
{
    uint32_t left = left_;
    if (running_) {
        left = static_cast<int32_t>(end_ - now) > 0 ? end_ - now : 0;
    }
    return {set_, left, running_, ringing_};
}

} // namespace core
