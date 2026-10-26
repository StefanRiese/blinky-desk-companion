// buddy_shot OUT_DIR: renders the screens without a window and writes PNG files:
// eyes-<mood>.png for every mood, dashboard.png (demo data), dashboard-empty.png,
// info.png and info-touch.png.
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
    ui::App *app = ui::App::create({.version = "sim", .board = "sim", .seed = 1, .store = &store, .settings = &settings,
                                    .local_time = [&](std::tm &t) { return time_set && sim::fixed_time(t); }});

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

    lv_screen_load(app->screen(ui::App::kDashboard));
    app->dashboard()->refresh();
    shot("dashboard-empty");
    sim::fill_demo(store);
    time_set = true;
    app->dashboard()->refresh();
    shot("dashboard");

    lv_screen_load(app->screen(ui::App::kInfo));
    shot("info");
    screen.touch(300, 230, true); // a tap, so the touch tester shows its dot and counter
    screen.touch(300, 230, false);
    shot("info-touch");

    std::printf("%s: wrote %d screenshots to %s\n", ok ? "ok" : "failed", n, dir.c_str());
    return ok ? 0 : 1;
}
