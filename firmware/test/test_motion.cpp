#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "app_core/motion.h"

using core::Gesture;
using core::MotionDetector;
using core::MotionSample;

namespace {

constexpr MotionSample kRest{0, 0, 1, 0}; // lying flat, still

// feed sends n samples 10 ms apart from t on and returns the gestures found.
std::vector<Gesture> feed(MotionDetector &m, uint32_t &t, int n, MotionSample (*f)(int))
{
    std::vector<Gesture> out;
    for (int i = 0; i < n; i++, t += 10) {
        if (Gesture g = m.add(f(i), t); g != Gesture::None) {
            out.push_back(g);
        }
    }
    return out;
}

} // namespace

TEST_CASE("MotionDetector: a shake, a flick each way, and nothing at rest")
{
    MotionDetector m;
    uint32_t t = 0;
    CHECK(feed(m, t, 100, [](int) { return kRest; }).empty());

    // Shaking left and right: ±2.5 g on X, reversing every 100 ms, for 0.8 s.
    CHECK(feed(m, t, 80, [](int i) { return MotionSample{(i / 10) % 2 ? -2.5f : 2.5f, 0, 1, 0}; }) ==
          std::vector<Gesture>{Gesture::Shake});
    CHECK(feed(m, t, 100, [](int) { return kRest; }).empty());

    // A twist clockwise (from the front) for 150 ms, then still: flick right.
    CHECK(feed(m, t, 30, [](int i) { return MotionSample{0, 0, 1, i < 15 ? -300.0f : 0.0f}; }) ==
          std::vector<Gesture>{Gesture::FlickRight});
    CHECK(feed(m, t, 100, [](int) { return kRest; }).empty());
    CHECK(feed(m, t, 30, [](int i) { return MotionSample{0, 0, 1, i < 15 ? 300.0f : 0.0f}; }) ==
          std::vector<Gesture>{Gesture::FlickLeft});

    // Turning the device slowly (the rotation lasts a second) is no flick.
    CHECK(feed(m, t, 100, [](int) { return kRest; }).empty());
    CHECK(feed(m, t, 120, [](int i) { return MotionSample{0, 0, 1, i < 100 ? 200.0f : 0.0f}; }).empty());
}

TEST_CASE("MotionDetector: a cooldown after a gesture, and taps on the desk don't count")
{
    MotionDetector m;
    uint32_t t = 0;
    feed(m, t, 50, [](int) { return kRest; });
    // Two flicks 300 ms apart: the second falls into the cooldown.
    auto g = feed(m, t, 60, [](int i) { return MotionSample{0, 0, 1, (i < 10 || (i >= 30 && i < 40)) ? -300.0f : 0.0f}; });
    CHECK(g == std::vector<Gesture>{Gesture::FlickRight});
    // A few single knocks (0.5 g) are far from a shake.
    CHECK(feed(m, t, 200, [](int i) { return MotionSample{i % 50 == 0 ? 0.5f : 0.0f, 0, 1, 0}; }).empty());
}
