// Countdown (timer page), same behaviour as the emulator (host/internal/sim/countdown.go):
// swipe up/down on the minutes or seconds to set it (wrapping 00:00…99:59), shake to start,
// pause and resume, long press to reset to the last set time. At zero it rings until shaken,
// at most one minute. Times are in milliseconds from a monotonic clock.
#pragma once

#include <cstdint>

namespace core {

// Action is a user input, from touch, buttons or (later) the IMU.
enum class Action { Tap, LongPress, SwipeUp, SwipeDown, SwipeLeft, SwipeRight, Shake };

class Countdown {
public:
    enum class Event { None, Started, Paused, Reset, Done, Stopped };
    static constexpr int kMaxMinutes = 99;
    static constexpr uint32_t kRingMs = 60'000;

    struct View {
        uint32_t set_ms, left_ms;
        bool running, ringing;
        bool paused() const { return !running && !ringing && left_ms > 0 && left_ms != set_ms; }
    };

    // input handles an action on the countdown page; x is where a swipe started (0…1 across
    // the screen; < 0 unknown = minutes). Returns whether the action was used.
    bool input(Action a, float x, uint32_t now_ms, Event *ev = nullptr);

    // tick runs every frame: reports Done at zero, and Stopped when ringing ends by itself.
    Event tick(uint32_t now_ms);

    View view(uint32_t now_ms) const;
    bool ringing() const { return ringing_; }

private:
    uint32_t set_ = 0, left_ = 0, end_ = 0, ring_start_ = 0;
    bool running_ = false, ringing_ = false;
};

} // namespace core
