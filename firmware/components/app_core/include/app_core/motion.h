// MotionDetector turns IMU readings into gestures (firmware/PLAN.md, Motion gestures):
//   shake: more than three changes of direction within a second, strong ones (> 1 g beyond
//          gravity);
//   flick: a quick twist about the screen's axis (gyro Z beyond 150 °/s) that comes back
//          within 400 ms; its sign says left or right.
// After a gesture, 600 ms pass before the next. The thresholds need checking on the board.
// Portable (no hardware): the sensors task feeds it, the tests too.
#pragma once

#include <cstdint>
#include <deque>

namespace core {

enum class Gesture { None, Shake, FlickLeft, FlickRight };

const char *gesture_name(Gesture g); // "shake", "flick_left", "flick_right" (protocol names)

struct MotionSample {
    float ax, ay, az; // acceleration in g
    float gz;         // rotation about the screen's axis in °/s (positive: counterclockwise from the front)
};

class MotionDetector {
public:
    static constexpr float kShakeG = 1.0f;          // beyond gravity
    static constexpr int kShakeTurns = 4;           // changes of direction …
    static constexpr uint32_t kShakeWindowMs = 1000; // … within this time
    static constexpr float kFlickDps = 150.0f;
    static constexpr float kRestDps = 40.0f;
    static constexpr uint32_t kFlickMs = 400;
    static constexpr uint32_t kCooldownMs = 600;

    // add takes one reading (about 100 per second) and returns a gesture when one is done.
    Gesture add(const MotionSample &s, uint32_t now_ms);

private:
    float gx_ = 0, gy_ = 0, gz_ = 0; // gravity estimate (low-pass)
    bool started_ = false;
    int last_sign_ = 0;                 // direction of the last strong push
    int last_axis_ = -1;
    std::deque<uint32_t> turns_;        // when the direction changed
    uint32_t flick_start_ = 0;
    int flick_sign_ = 0;                // 0 = no twist under way
    bool need_rest_ = false;            // a twist was too slow: wait until it stops
    uint32_t quiet_until_ = 0;          // cooldown after a gesture
};

} // namespace core
