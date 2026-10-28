// buddy_shot OUT_DIR: renders the screens without a window and writes PNG files:
// eyes-<mood>.png for every mood, dashboard(-empty).png, clock.png, countdown-*.png, alarm*.png,
// info.png, info-links.png, info-touch.png and eyes-status.png.
#include <cstdio>
#include <string>

#include "demo.h"
#include "headless.h"
#include "ui/app.h"

int main(int argc, char **argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    sim::Headless screen(412, 412);
    core::DataStore store([] { return lv_tick_get(); });
    core::MemoryStorage storage;
    core::Settings settings(storage);
    bool time_set = false; // the clock starts unset, like a device without RTC time
    core::Status status;   // nothing connected
    ui::App *app = ui::App::create({.version = "sim", .board = "sim", .seed = 1, .store = &store, .settings = &settings,
                                    .local_time = [&](std::tm &t) { return time_set && sim::fixed_time(t); },
                                    .gestures = {},
                                    .status = [&] { return status; },
                                    .backlight = nullptr});

    bool ok = true;
    int n = 0;
    auto shot = [&](const std::string &name) {
        screen.render();
        ok = screen.write_png(dir + "/" + name + ".png") && ok;
        n++;
    };
    for (std::string_view mood : ui::moods()) {
        app->set_mood(mood);
        app->eyes()->snap();
        shot("eyes-" + std::string(mood));
    }

    app->show(ui::App::kDashboard);
    app->dashboard()->refresh();
    shot("dashboard-empty");
    sim::fill_demo(store);
    time_set = true;
    app->dashboard()->refresh();
    shot("dashboard");

    app->show(ui::App::kClock);
    app->clock()->refresh();
    shot("clock");

    // Countdown: set to 3:05, running, time's up.
    using core::Action;
    app->show(ui::App::kCountdown);
    shot("countdown-empty");
    for (int i = 0; i < 3; i++) {
        app->input(Action::SwipeUp, 0.25f);
    }
    for (int i = 0; i < 5; i++) {
        app->input(Action::SwipeUp, 0.75f);
    }
    shot("countdown-set");
    app->input(Action::Shake);
    shot("countdown-running");
    app->input(Action::Shake);
    app->input(Action::LongPress);
    app->countdown().input(Action::Shake, -1, lv_tick_get());
    app->countdown().tick(lv_tick_get() + 4 * 60000); // jump past the end
    app->tick();
    shot("countdown-ringing");
    app->input(Action::Shake);

    // Alarm: the factory alarms (07:00 Mon–Fri, 09:00 weekends), switched on, ringing.
    app->show(ui::App::kAlarm);
    app->input(Action::Tap);
    shot("alarm");
    app->alarms().tick(nullptr, lv_tick_get()); // nothing
    std::tm t{};
    sim::fixed_time(t);
    t.tm_hour = 7;
    t.tm_min = 0;
    app->alarms().tick(&t, lv_tick_get());
    app->tick();
    shot("alarm-ringing");
    app->input(Action::Shake);

    app->show(ui::App::kInfo);
    shot("info");
    status = sim::demo_status();
    app->info()->refresh();
    shot("info-links");
    screen.touch(300, 230, true); // a tap, so the touch tester shows its dot and counter
    screen.touch(300, 230, false);
    shot("info-touch");

    app->show(ui::App::kEyes);
    app->set_mood("neutral");
    app->eyes()->snap();
    shot("eyes-status");

    std::printf("%s: wrote %d screenshots to %s\n", ok ? "ok" : "failed", n, dir.c_str());
    return ok ? 0 : 1;
}
