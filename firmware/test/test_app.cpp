#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "fixture.h"

void AppFixture::swipe(int from_x, int to_x)
{
    const int step = from_x < to_x ? 20 : -20;
    screen.touch(from_x, 206, true);
    for (int x = from_x + step; step > 0 ? x <= to_x : x >= to_x; x += step) {
        screen.touch(x, 206, true);
    }
    screen.touch(to_x, 206, false);
    for (int i = 0; i < 20; i++) { // let the 250 ms slide animation finish
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        lv_timer_handler();
    }
}

TEST_CASE("tapping the eyes cycles through the moods")
{
    AppFixture f;
    CHECK(f.app->eyes()->mood() == "neutral");
    f.screen.touch(206, 206, true);
    f.screen.touch(206, 206, false);
    CHECK(f.app->eyes()->mood() == ui::moods()[1]);
    f.screen.touch(206, 206, true);
    f.screen.touch(206, 206, false);
    CHECK(f.app->eyes()->mood() == ui::moods()[2]);
}

TEST_CASE("swipes move through eyes, dashboard and info in a circle, and are not taps")
{
    AppFixture f;
    f.swipe(330, 90); // left: next
    CHECK(lv_screen_active() == f.app->screen(ui::App::kDashboard));
    CHECK(f.app->eyes()->mood() == "neutral");
    f.swipe(330, 90);
    CHECK(lv_screen_active() == f.app->screen(ui::App::kInfo));
    f.swipe(330, 90);
    CHECK(lv_screen_active() == f.app->screen(ui::App::kEyes)); // wrapped around
    f.swipe(90, 330); // right: previous
    CHECK(lv_screen_active() == f.app->screen(ui::App::kInfo));
}
