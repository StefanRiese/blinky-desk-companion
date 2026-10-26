#include <doctest/doctest.h>

#include "headless.h"
#include "ui/hello.h"
#include "ui/theme.h"

namespace {
uint32_t hex(lv_color_t c)
{
    return lv_color_to_u32(c) & 0xFFFFFF;
}
} // namespace

TEST_CASE("hello screen renders on the round display")
{
    sim::Headless screen(412, 412);
    ui::create_hello(lv_screen_active(), {.version = "test", .board = "test"});
    screen.render();

    CHECK(screen.pixel(0, 0) == hex(ui::theme::background()));         // corner: outside the circle
    CHECK(screen.pixel(206 - 56, 206 - 46) == hex(ui::theme::accent())); // centre of the left eye
    CHECK(screen.pixel(206 + 56, 206 - 46) == hex(ui::theme::accent())); // right eye
    CHECK(screen.pixel(206, 2) == hex(ui::theme::rim()));               // rim ring at the top
}

TEST_CASE("touch tester counts taps")
{
    sim::Headless screen(412, 412);
    ui::create_hello(lv_screen_active(), {.version = "test", .board = "test"});
    CHECK(ui::touches() == 0);
    screen.touch(300, 230, true);
    screen.touch(300, 230, false);
    CHECK(ui::touches() == 1);
    screen.touch(120, 300, true);
    screen.touch(120, 300, false);
    CHECK(ui::touches() == 2);
}
