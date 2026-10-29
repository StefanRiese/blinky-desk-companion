#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "fixture.h"
#include "ui/round_layout.h"

namespace {

// brightness sums the pixels of a strip of the screen (x0..x1, y0..y1).
long brightness(const sim::Headless &s, int x0, int x1, int y0, int y1)
{
    long sum = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const uint32_t p = s.pixel(x, y);
            sum += (p >> 16 & 0xFF) + (p >> 8 & 0xFF) + (p & 0xFF);
        }
    }
    return sum;
}

} // namespace

TEST_CASE("the display turns in 90° steps: the status bar moves from the top to the right")
{
    AppFixture f;
    sim::fill_demo(f.store);
    f.time_set = true;
    f.app->show(ui::App::kEyes);
    f.screen.render();
    const int n = f.screen.width();
    const long top = brightness(f.screen, n / 3, 2 * n / 3, 20, 50); // the status bar
    CHECK(top > 0);

    lv_display_set_rotation(f.screen.display(), ui::display_rotation(90)); // clockwise
    f.screen.render();
    CHECK(brightness(f.screen, n - 50, n - 20, n / 3, 2 * n / 3) > top / 2); // now on the right
    CHECK(brightness(f.screen, n / 3, 2 * n / 3, 20, 50) < top / 4);

    // A swipe from right to left as the user sees the turned picture still means "next
    // page". The touch controller reports panel coordinates: the user's right is the
    // panel's bottom, so the swipe goes up the panel. LVGL turns the points itself.
    f.screen.touch(206, 330, true);
    for (int y = 310; y >= 90; y -= 20) {
        f.screen.touch(206, y, true);
    }
    f.screen.touch(206, 90, false);
    for (int i = 0; i < 20; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        lv_timer_handler();
    }
    CHECK(f.app->current() == ui::App::kDashboard);
    lv_display_set_rotation(f.screen.display(), LV_DISPLAY_ROTATION_0);
}
