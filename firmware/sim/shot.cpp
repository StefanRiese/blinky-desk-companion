// buddy_shot OUT_DIR: renders the screens without a window and writes PNG files:
// eyes-<mood>.png for every mood, info.png and info-touch.png.
#include <cstdio>
#include <string>

#include "headless.h"
#include "ui/app.h"
#include "ui/hello.h"

int main(int argc, char **argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    sim::Headless screen(412, 412);
    ui::App *app = ui::App::create({.version = "sim", .board = "sim", .seed = 1});

    bool ok = true;
    int n = 0;
    for (std::string_view mood : ui::moods()) {
        app->eyes()->set_mood(mood);
        app->eyes()->snap();
        screen.render();
        ok = screen.write_png(dir + "/eyes-" + std::string(mood) + ".png") && ok;
        n++;
    }

    lv_screen_load(app->info_screen());
    screen.render();
    ok = screen.write_png(dir + "/info.png") && ok;
    screen.touch(300, 230, true); // a tap, so the touch tester shows its dot and counter
    screen.touch(300, 230, false);
    screen.render();
    ok = screen.write_png(dir + "/info-touch.png") && ok;

    std::printf("%s: wrote %d eye screenshots, info.png and info-touch.png to %s\n", ok ? "ok" : "failed", n,
                dir.c_str());
    return ok ? 0 : 1;
}
