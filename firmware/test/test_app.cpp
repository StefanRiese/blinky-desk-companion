#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "headless.h"
#include "ui/app.h"

TEST_CASE("tapping the eyes cycles through the moods")
{
    sim::Headless screen(412, 412);
    ui::App *app = ui::App::create({.version = "test", .board = "test", .seed = 1});
    CHECK(app->eyes()->mood() == "neutral");
    screen.touch(206, 206, true);
    screen.touch(206, 206, false);
    CHECK(app->eyes()->mood() == ui::moods()[1]);
    screen.touch(206, 206, true);
    screen.touch(206, 206, false);
    CHECK(app->eyes()->mood() == ui::moods()[2]);
}

TEST_CASE("a swipe left opens the info screen, and does not count as a tap")
{
    sim::Headless screen(412, 412);
    ui::App *app = ui::App::create({.version = "test", .board = "test", .seed = 1});
    screen.touch(330, 206, true);
    for (int x = 310; x >= 90; x -= 20) { // drag left
        screen.touch(x, 206, true);
    }
    screen.touch(90, 206, false);
    for (int i = 0; i < 20; i++) { // let the 250 ms slide animation finish
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        lv_timer_handler();
    }
    CHECK(app->eyes()->mood() == "neutral");
    CHECK(lv_screen_active() == app->info_screen());
}
