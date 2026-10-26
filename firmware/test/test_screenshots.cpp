// Screenshot tests: render each screen and compare it with the reference PNG in
// test/screenshots/. After an intended change, regenerate the references with
//   UPDATE_SCREENSHOTS=1 ctest --test-dir <build>
// and look at the new images before committing them.
#include <doctest/doctest.h>

#include <cstdlib>
#include <string>

#include "fixture.h"
#include "png.h"

namespace {

const std::string kRefDir = SCREENSHOT_DIR;

// Rendering differs by a few anti-aliased pixels between compilers; allow that, but not
// a moved or missing shape.
constexpr int kChannelTolerance = 24;
constexpr double kMaxDifferent = 0.003; // share of pixels

void check_screenshot(const sim::Headless &screen, const std::string &name)
{
    const std::string ref = kRefDir + "/" + name + ".png";
    if (std::getenv("UPDATE_SCREENSHOTS") != nullptr) {
        REQUIRE(screen.write_png(ref));
        return;
    }
    int w = 0, h = 0;
    std::vector<uint8_t> want;
    REQUIRE_MESSAGE(png::read_rgb(ref, w, h, want), "missing or unreadable reference " << ref << " (regenerate with UPDATE_SCREENSHOTS=1; other PNG tools write filtered files this reader does not read)");
    REQUIRE(w == screen.width());
    REQUIRE(h == screen.height());
    const std::vector<uint8_t> got = screen.rgb();
    int different = 0;
    for (size_t i = 0; i < got.size(); i += 3) {
        for (size_t c = 0; c < 3; c++) {
            if (std::abs(int{got[i + c]} - int{want[i + c]}) > kChannelTolerance) {
                different++;
                break;
            }
        }
    }
    const double share = static_cast<double>(different) / (w * h);
    if (share > kMaxDifferent) {
        screen.write_png(name + ".actual.png"); // in the test's working directory
    }
    CHECK_MESSAGE(share <= kMaxDifferent, name << ": " << different << " pixels differ (see " << name << ".actual.png)");
}

} // namespace

TEST_CASE("screenshots: eyes in every mood, dashboard, info screen")
{
    AppFixture f;
    for (std::string_view mood : ui::moods()) {
        f.app->set_mood(mood);
        f.app->eyes()->snap();
        f.screen.render();
        check_screenshot(f.screen, "eyes-" + std::string(mood));
    }

    lv_screen_load(f.app->screen(ui::App::kDashboard));
    f.app->dashboard()->refresh();
    f.screen.render();
    check_screenshot(f.screen, "dashboard-empty");
    sim::fill_demo(f.store);
    f.time_set = true;
    f.app->dashboard()->refresh();
    f.screen.render();
    check_screenshot(f.screen, "dashboard");
    f.settings.set("units.temp", "F");
    f.settings.set("units.wind", "mph");
    f.app->dashboard()->refresh();
    f.screen.render();
    check_screenshot(f.screen, "dashboard-imperial");

    lv_screen_load(f.app->screen(ui::App::kInfo));
    f.screen.render();
    check_screenshot(f.screen, "info");
}
