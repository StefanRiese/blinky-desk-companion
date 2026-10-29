#include "app_core/motion.h"

#include <cmath>

namespace core {

const char *gesture_name(Gesture g)
{
    switch (g) {
    case Gesture::Shake:
        return "shake";
    case Gesture::FlickLeft:
        return "flick_left";
    case Gesture::FlickRight:
        return "flick_right";
    case Gesture::None:
        break;
    }
    return "";
}

Gesture MotionDetector::add(const MotionSample &s, uint32_t now_ms)
{
    if (!started_) {
        started_ = true;
        gx_ = s.ax, gy_ = s.ay, gz_ = s.az;
    }
    // Gravity follows slowly; what remains is the hand moving the device.
    constexpr float kAlpha = 0.05f;
    gx_ += (s.ax - gx_) * kAlpha;
    gy_ += (s.ay - gy_) * kAlpha;
    gz_ += (s.az - gz_) * kAlpha;
    const float d[3] = {s.ax - gx_, s.ay - gy_, s.az - gz_};

    const bool quiet = static_cast<int32_t>(now_ms - quiet_until_) < 0;

    // Shake: strong pushes that change direction (on the strongest axis) several times.
    int axis = 0;
    for (int i = 1; i < 3; i++) {
        if (std::fabs(d[i]) > std::fabs(d[axis])) {
            axis = i;
        }
    }
    if (std::fabs(d[axis]) > kShakeG) {
        const int sign = d[axis] > 0 ? 1 : -1;
        if (axis == last_axis_ && sign != last_sign_ && last_sign_ != 0) {
            turns_.push_back(now_ms);
        }
        last_sign_ = sign;
        last_axis_ = axis;
    }
    while (!turns_.empty() && now_ms - turns_.front() > kShakeWindowMs) {
        turns_.pop_front();
    }
    if (static_cast<int>(turns_.size()) >= kShakeTurns) {
        turns_.clear();
        last_sign_ = 0;
        flick_sign_ = 0;
        if (!quiet) {
            quiet_until_ = now_ms + kCooldownMs;
            return Gesture::Shake;
        }
    }

    // Flick: a quick twist one way that settles again soon. A shake in progress isn't one.
    if (flick_sign_ == 0) {
        if (need_rest_ && std::fabs(s.gz) < kRestDps) {
            need_rest_ = false;
        }
        if (!need_rest_ && std::fabs(s.gz) > kFlickDps && turns_.empty()) {
            flick_sign_ = s.gz > 0 ? 1 : -1;
            flick_start_ = now_ms;
        }
    } else if (now_ms - flick_start_ > kFlickMs) {
        flick_sign_ = 0; // too slow: turning the device, not flicking it
        need_rest_ = true;
    } else if (std::fabs(s.gz) < kRestDps && !turns_.empty()) {
        flick_sign_ = 0;
    } else if (std::fabs(s.gz) < kRestDps) {
        const int sign = flick_sign_;
        flick_sign_ = 0;
        if (!quiet) {
            quiet_until_ = now_ms + kCooldownMs;
            // Counterclockwise from the front turns the top to the left.
            return sign > 0 ? Gesture::FlickLeft : Gesture::FlickRight;
        }
    }
    return Gesture::None;
}

} // namespace core
