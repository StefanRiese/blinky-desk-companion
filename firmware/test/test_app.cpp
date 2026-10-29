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
    const ui::App::Screen order[] = {ui::App::kDashboard, ui::App::kClock,     ui::App::kForecast, ui::App::kIndoor,
                                     ui::App::kHost,      ui::App::kCountdown, ui::App::kAlarm,    ui::App::kInfo,
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

TEST_CASE("ui.pages picks and orders the pages; swipe up goes home")
{
    using core::Action;
    using ui::App;
    AppFixture f;
    CHECK(f.settings.set("ui.pages", R"(["nope"])").has_value());
    CHECK(f.settings.set("ui.pages", R"([])").has_value());
    CHECK(f.settings.set("ui.pages", R"(["eyes","eyes"])").has_value());

    // The info screen stays last.
    REQUIRE_FALSE(f.settings.set("ui.pages", R"(["clock","eyes","forecast"])").has_value());
    f.app->tick();
    CHECK(f.app->pages() == std::vector<App::Screen>{App::kClock, App::kEyes, App::kForecast, App::kInfo});
    CHECK(f.app->current() == App::kEyes);
    for (App::Screen s : {App::kForecast, App::kInfo, App::kClock, App::kEyes}) {
        f.app->input(Action::SwipeLeft);
        CHECK(f.app->current() == s);
    }
    f.app->input(Action::SwipeRight);
    CHECK(f.app->current() == App::kClock);
    f.app->input(Action::SwipeUp);
    CHECK(f.app->current() == App::kEyes); // home: the eyes

    // Without the eyes, home is the first page; the page shown leaves when it is hidden.
    REQUIRE_FALSE(f.settings.set("ui.pages", R"(["dashboard","clock"])").has_value());
    f.app->tick();
    CHECK(f.app->home() == App::kDashboard);
    CHECK(f.app->current() == App::kDashboard);
    f.app->input(Action::SwipeLeft);
    CHECK(f.app->current() == App::kClock);
    f.app->input(Action::SwipeUp);
    CHECK(f.app->current() == App::kDashboard);
}

TEST_CASE("a hidden countdown page stays while its countdown runs")
{
    using core::Action;
    using ui::App;
    AppFixture f;
    f.app->show(App::kCountdown);
    f.app->input(Action::SwipeUp, 0.75f); // 1 second
    f.app->input(Action::Shake);          // start
    REQUIRE(f.app->countdown().running());

    // Hidden while it runs: the page stays until the countdown is over.
    REQUIRE_FALSE(f.settings.set("ui.pages", R"(["eyes"])").has_value());
    f.app->tick();
    CHECK(f.app->current() == App::kCountdown);
    CHECK(f.app->pages() == std::vector<App::Screen>{App::kEyes, App::kCountdown, App::kInfo});
    f.app->input(Action::LongPress); // reset: nothing runs any more
    CHECK(f.app->current() == App::kEyes);
    CHECK(f.app->pages() == std::vector<App::Screen>{App::kEyes, App::kInfo});
}

TEST_CASE("the status bar shows a running countdown on other pages, and an alarm that is on")
{
    using core::Action;
    using ui::App;
    AppFixture f;
    CHECK(f.app->timers().countdown.empty());
    f.app->show(App::kCountdown);
    for (int i = 0; i < 3; i++) {
        f.app->input(Action::SwipeUp, 0.25f); // 3 minutes
    }
    f.app->input(Action::Shake); // start
    CHECK(f.app->timers().countdown.empty()); // its own page shows it already
    f.app->show(App::kEyes);
    const std::string left = f.app->timers().countdown;
    CHECK((left == "03:00" || left == "02:59"));

    CHECK_FALSE(f.app->timers().alarm);
    f.settings.set("alarm.list", R"([{"at":"07:00","on":true}])");
    CHECK(f.app->timers().alarm);
    f.app->show(App::kAlarm);
    CHECK_FALSE(f.app->timers().alarm); // the alarm page shows it
}

TEST_CASE("a notification shows over every page, chimes, and goes away")
{
    using core::Action;
    using ui::App;
    AppFixture f;
    CHECK_FALSE(f.app->notifying());
    f.app->notify({.text = "Pipeline failed: main", .icon = "ci_fail", .mood = "sad", .sound = "beep", .dur = 1});
    CHECK(f.app->notifying());
    CHECK(f.app->tune() == core::Tune::Notify);
    CHECK(f.app->shown_mood().name == "sad");
    f.app->input(Action::Tap); // a tap hides it (and does nothing else)
    CHECK_FALSE(f.app->notifying());
    CHECK(f.app->tune() == core::Tune::None);

    f.app->notify({.text = "Coffee is ready", .icon = "", .mood = "", .sound = "", .dur = 1});
    CHECK(f.app->tune() == core::Tune::None); // no sound asked for
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    f.app->tick();
    CHECK_FALSE(f.app->notifying());
}
