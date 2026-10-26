#include <doctest/doctest.h>

#include "ui/round_layout.h"

TEST_CASE("visible_width follows the circle")
{
    const int d = 412;
    CHECK(ui::visible_width(d, d / 2) >= d - 2);     // full width through the middle
    CHECK(ui::visible_width(d, 0) < 60);             // only a sliver at the top row
    CHECK(ui::visible_width(d, d - 1) < 60);         // and at the bottom row
    CHECK(ui::visible_width(d, 0) == ui::visible_width(d, d - 1)); // symmetric
    CHECK(ui::visible_width(d, -1) == 0);            // outside
    CHECK(ui::visible_width(d, d) == 0);
    CHECK(ui::visible_width(d, d / 2, 8) == ui::visible_width(d, d / 2) - 16);
}

TEST_CASE("fits checks the narrowest row of a centred box")
{
    const int d = 412;
    CHECK(ui::fits(d, 61, 290, 290));      // the inner square of the plan
    CHECK_FALSE(ui::fits(d, 0, 100, 20));  // too close to the top
    CHECK_FALSE(ui::fits(d, 150, 420, 40)); // wider than the screen
    CHECK_FALSE(ui::fits(d, 400, 20, 20)); // runs off the bottom
}
