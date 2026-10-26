// buddy_shot OUT_DIR: renders the screens without a window and writes PNG files, for
// looking at the UI in CI and for screenshot tests.
#include <cstdio>
#include <string>

#include "headless.h"
#include "ui/hello.h"

int main(int argc, char **argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    sim::Headless screen(412, 412);
    ui::create_hello(lv_screen_active(), {.version = "sim", .board = "sim"});

    screen.render();
    bool ok = screen.write_png(dir + "/hello.png");

    // A tap, so the touch tester shows its dot and counter.
    screen.touch(300, 230, true);
    screen.touch(300, 230, false);
    screen.render();
    ok = screen.write_png(dir + "/hello-touch.png") && ok;

    std::printf("%s: wrote %s/hello.png and hello-touch.png\n", ok ? "ok" : "failed", dir.c_str());
    return ok ? 0 : 1;
}
