#include <doctest/doctest.h>

#include <cmath>
#include <string_view>

#include "ui/eye_model.h"

TEST_CASE("presets: all moods known, unknown mood falls back to neutral")
{
    CHECK(ui::moods().size() == 16);
    CHECK(ui::preset("angry").color == 0xFF5A5A);
    CHECK(ui::preset("happy").bottom > 0.3f);
    CHECK(ui::preset("no-such-mood").color == ui::preset("neutral").color);

    ui::EyeAnimator a;
    a.set_mood("no-such-mood");
    CHECK(a.mood() == "neutral");
}

TEST_CASE("animator moves smoothly towards the preset")
{
    ui::EyeAnimator a(7);
    a.set_mood("sleepy");
    a.update(1000);
    const float first = a.frame().shape.top;
    CHECK(first > 0.0f);
    CHECK(first < ui::preset("sleepy").top); // not there yet after one frame
    for (uint32_t t = 1033; t < 4000; t += 33) {
        a.update(t);
    }
    CHECK(a.frame().shape.top == doctest::Approx(ui::preset("sleepy").top).epsilon(0.02));
}

TEST_CASE("animator blinks and glances, and the same seed gives the same eyes")
{
    ui::EyeAnimator a(42), b(42);
    bool blinked = false, glanced = false;
    for (uint32_t t = 16; t < 12000; t += 16) {
        a.update(t);
        b.update(t);
        const ui::EyeFrame fa = a.frame(), fb = b.frame();
        CHECK(fa.blink == fb.blink);
        CHECK(fa.look_x == fb.look_x);
        blinked = blinked || fa.blink < 0.5f;
        glanced = glanced || fa.look_x > 3 || fa.look_x < -3;
    }
    CHECK(blinked);
    CHECK(glanced);
}

TEST_CASE("idle eyes look far to both sides and blink twice in a row")
{
    ui::EyeAnimator a(5);
    a.set_idle(true);
    bool left = false, right = false, double_blink = false;
    uint32_t last_blink_end = 0;
    bool was_closed = false;
    for (uint32_t t = 16; t < 60000; t += 16) {
        a.update(t);
        const ui::EyeFrame f = a.frame();
        left = left || f.look_x < -35;
        right = right || f.look_x > 35;
        const bool closed = f.blink < 0.5f;
        if (closed && !was_closed && last_blink_end != 0 && t - last_blink_end < 400) {
            double_blink = true;
        }
        if (!closed && was_closed) {
            last_blink_end = t;
        }
        was_closed = closed;
    }
    CHECK(left);
    CHECK(right);
    CHECK(double_blink);

    a.set_idle(false); // back to small glances
    for (uint32_t t = 60000; t < 62000; t += 16) {
        a.update(t);
    }
    for (uint32_t t = 62000; t < 80000; t += 16) {
        a.update(t);
        CHECK(std::abs(a.frame().look_x) <= 31);
    }
}

TEST_CASE("snap shows the preset at once and stops the animation")
{
    ui::EyeAnimator a(3);
    a.set_mood("angry");
    a.snap();
    for (uint32_t t = 16; t < 10000; t += 16) {
        a.update(t);
    }
    const ui::EyeFrame f = a.frame();
    CHECK(f.shape.top == ui::preset("angry").top);
    CHECK(f.blink == 1.0f);
    CHECK(f.look_x == 0.0f);
    a.set_mood("happy"); // a new mood starts the animation again
    a.update(10016);
    CHECK(a.frame().shape.bottom > 0.0f);
}

TEST_CASE("eye_rect places the eyes symmetrically")
{
    ui::EyeFrame f;
    const ui::EyeRect l = ui::eye_rect(f, -1, 206, 218, 1), r = ui::eye_rect(f, 1, 206, 218, 1);
    CHECK(l.w == doctest::Approx(ui::kEyeW));
    CHECK(l.h == doctest::Approx(ui::kEyeH));
    CHECK(l.h > 1.5f * l.w); // an upright pill, as in the README's GIF
    CHECK(l.radius == doctest::Approx(ui::kEyeRadius));
    CHECK((l.x + l.w / 2) == doctest::Approx(206 - ui::kEyeApart));
    CHECK((r.x + r.w / 2) == doctest::Approx(206 + ui::kEyeApart));
    CHECK(l.y == r.y);
    f.blink = 0.06f;
    CHECK(ui::eye_rect(f, -1, 206, 218, 1).h < 10); // closed while blinking
}

TEST_CASE("eye colour: ui.color for moods without their own colour")
{
    ui::EyeAnimator a(1);
    a.set_color(0x4ADE80);
    a.set_mood("happy");
    a.snap();
    CHECK(a.frame().shape.color == 0x4ADE80);
    a.set_mood("angry"); // red stays red
    a.snap();
    CHECK(a.frame().shape.color == ui::preset("angry").color);
}
