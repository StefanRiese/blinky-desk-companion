#include <doctest/doctest.h>

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
    CHECK(l.w == doctest::Approx(108));
    CHECK(l.h == doctest::Approx(132));
    CHECK((l.x + l.w / 2) == doctest::Approx(206 - 92));
    CHECK((r.x + r.w / 2) == doctest::Approx(206 + 92));
    CHECK(l.y == r.y);
    f.blink = 0.06f;
    CHECK(ui::eye_rect(f, -1, 206, 218, 1).h < 10); // closed while blinking
}
