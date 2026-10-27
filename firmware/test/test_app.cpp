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

TEST_CASE("a tap on the eyes is a boop: happy for a moment")
{
    AppFixture f;
    f.app->tick();
    CHECK(f.app->eyes()->mood() == "neutral");
    f.screen.touch(206, 206, true);
    f.screen.touch(206, 206, false);
    CHECK(f.app->eyes()->mood() == "happy");
    CHECK(f.app->shown_mood().reason == "boop");
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    f.app->tick();
    CHECK(f.app->eyes()->mood() == "neutral");
}

TEST_CASE("swipes move through all screens in a circle, and are not taps")
{
    AppFixture f;
    const ui::App::Screen order[] = {ui::App::kDashboard, ui::App::kCountdown, ui::App::kAlarm, ui::App::kInfo,
                                     ui::App::kEyes};
    for (ui::App::Screen s : order) {
        f.swipe(330, 90); // left: next
        CHECK(lv_screen_active() == f.app->screen(s));
        CHECK(f.app->current() == s);
    }
    CHECK(f.app->eyes()->mood() == "neutral");
    f.swipe(90, 330); // right: previous
    CHECK(f.app->current() == ui::App::kInfo);
}

TEST_CASE("countdown page: touch sets and starts it, it rings and shows itself, eyes surprised")
{
    using core::Action;
    AppFixture f;
    f.app->show(ui::App::kCountdown);
    f.app->input(Action::SwipeUp, 0.75f); // 0:01
    CHECK(f.app->countdown().view(lv_tick_get()).set_ms == 1000);
    f.screen.touch(206, 206, true); // a tap starts it while there is no shake
    f.screen.touch(206, 206, false);
    CHECK(f.app->countdown().view(lv_tick_get()).running);

    f.app->show(ui::App::kEyes);
    f.app->countdown().tick(lv_tick_get() + 1500); // past the end …
    f.app->countdown().input(Action::LongPress, -1, 0);
    f.app->countdown().input(Action::Shake, -1, lv_tick_get());
    f.app->countdown().tick(lv_tick_get() + 1500);
    f.app->tick(); // … rings: eyes surprised
    CHECK(f.app->countdown().ringing());
    CHECK(f.app->eyes()->mood() == "surprised");
    f.app->input(Action::Shake);
    CHECK_FALSE(f.app->countdown().ringing());
    CHECK(f.app->eyes()->mood() == "neutral"); // back to the mood before
}

TEST_CASE("alarm page: tap switches it on; a ringing alarm jumps to its page and takes all input")
{
    using core::Action;
    AppFixture f;
    f.time_set = true; // Monday 07:16
    f.app->show(ui::App::kAlarm);
    f.app->input(Action::Tap);
    CHECK(f.alarms_on() == 1);

    f.app->show(ui::App::kDashboard);
    std::tm t{};
    sim::fixed_time(t);
    t.tm_wday = 2; // Tuesday 07:00
    t.tm_hour = 7;
    t.tm_min = 0;
    f.app->alarms().tick(&t, lv_tick_get());
    f.app->tick();
    CHECK(f.app->alarms().ringing());
    f.app->show(ui::App::kAlarm);
    f.app->input(Action::SwipeLeft); // blocked while ringing
    CHECK(f.app->current() == ui::App::kAlarm);
    f.screen.touch(206, 206, true); // tap = snooze
    f.screen.touch(206, 206, false);
    CHECK_FALSE(f.app->alarms().ringing());
    CHECK(f.app->alarms().snoozed_until() == "07:25");
}
