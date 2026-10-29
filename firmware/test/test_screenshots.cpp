// Screenshot tests: render each screen and compare it with the reference PNG in
// test/screenshots/. After an intended change, regenerate the references with
//   UPDATE_SCREENSHOTS=1 ctest --test-dir <build>
// and look at the new images before committing them.
#include <doctest/doctest.h>

#include <cstdlib>
#include <string>

#include "fixture.h"
#include "png.h"
#include "ui/round_layout.h"

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

TEST_CASE("screenshots: eyes in every mood, dashboard, clock, data pages, countdown, alarm, info screen, status bar")
{
    AppFixture f;
    for (std::string_view mood : ui::moods()) {
        f.app->set_mood(mood);
        f.app->eyes()->snap();
        f.screen.render();
        check_screenshot(f.screen, "eyes-" + std::string(mood));
    }

    f.app->show(ui::App::kDashboard);
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

    f.app->show(ui::App::kClock);
    f.app->clock()->refresh();
    f.screen.render();
    check_screenshot(f.screen, "clock");

    f.app->show(ui::App::kEyes);
    f.app->set_mood("sad");
    f.app->eyes()->snap();
    f.app->notify({.text = "Pipeline failed: main", .icon = "ci_fail", .mood = "", .sound = "", .dur = 8});
    f.screen.render();
    check_screenshot(f.screen, "notification");
    f.app->input(core::Action::Tap);

    f.app->show(ui::App::kForecast);
    f.app->forecast()->refresh();
    f.screen.render();
    check_screenshot(f.screen, "forecast");
    // The indoor trend: a sample every 10 s; here the temperature rises and falls a little.
    for (double t : {21.2, 21.3, 21.3, 21.5, 21.6, 21.8, 21.7, 21.6, 21.6, 21.4, 21.5, 21.6}) {
        f.store.put("indoor.temp", t, core::Origin::Local, 0);
        f.app->indoor()->sample();
    }
    f.app->show(ui::App::kIndoor);
    f.app->indoor()->refresh();
    f.screen.render();
    check_screenshot(f.screen, "indoor");
    f.app->show(ui::App::kHost);
    f.app->host_data()->refresh();
    f.screen.render();
    check_screenshot(f.screen, "host-data");

    {
        JsonDocument doc;
        deserializeJson(doc, R"({"t":"page","name":"ci","title":"GitLab","items":[)"
                             R"({"w":"icon+text","icon":"ok","text":"main"},)"
                             R"({"w":"icon+text","icon":"fail","text":"feature/x"},)"
                             R"({"w":"value","label":"MRs to review","value":2},)"
                             R"({"w":"bar","label":"runner disk","value":86,"unit":"%","warn":80,"crit":90},)"
                             R"({"w":"sparkline","label":"queue","values":[3,5,4,8,6,9,7,4,2,3,5]}]})");
        f.app->custom_pages().handle(doc.as<JsonVariantConst>(), lv_tick_get());
        f.app->show_custom("ci");
        f.screen.render();
        check_screenshot(f.screen, "custom-page");
    }

    using core::Action;
    f.app->show(ui::App::kCountdown);
    f.app->tick();
    f.screen.render();
    check_screenshot(f.screen, "countdown-empty");
    for (int i = 0; i < 3; i++) {
        f.app->input(Action::SwipeUp, 0.25f);
    }
    for (int i = 0; i < 5; i++) {
        f.app->input(Action::SwipeUp, 0.75f);
    }
    f.screen.render();
    check_screenshot(f.screen, "countdown-set");
    f.app->input(Action::Shake);
    f.screen.render();
    check_screenshot(f.screen, "countdown-running");
    f.app->input(Action::LongPress);

    f.app->show(ui::App::kAlarm);
    f.app->input(Action::Tap);
    f.screen.render();
    check_screenshot(f.screen, "alarm");

    f.app->show(ui::App::kInfo);
    f.screen.render();
    check_screenshot(f.screen, "info");
    f.status = sim::demo_status();
    f.app->info()->refresh();
    f.screen.render();
    check_screenshot(f.screen, "info-links");

    f.app->show(ui::App::kEyes); // with the status bar: time, temperature, WiFi and USB
    f.app->set_mood("neutral");
    f.app->eyes()->snap();
    f.screen.render();
    check_screenshot(f.screen, "eyes-status");
}

TEST_CASE("screenshots: the 466 px AMOLED keeps the layout centred")
{
    AppFixture f(466);
    sim::fill_demo(f.store);
    f.time_set = true;
    f.status = sim::demo_status();
    f.app->show(ui::App::kEyes);
    f.app->set_mood("neutral");
    f.app->eyes()->snap();
    f.screen.render();
    check_screenshot(f.screen, "466-eyes-status");

    f.app->show(ui::App::kDashboard);
    f.app->dashboard()->refresh();
    f.screen.render();
    check_screenshot(f.screen, "466-dashboard");

    // ui.rotation = 90: the picture turned clockwise (the Blinky stands on its side).
    lv_display_set_rotation(f.screen.display(), ui::display_rotation(90));
    f.screen.render();
    check_screenshot(f.screen, "466-dashboard-rotated");
    lv_display_set_rotation(f.screen.display(), LV_DISPLAY_ROTATION_0);

    f.app->show(ui::App::kClock);
    f.app->clock()->refresh();
    f.screen.render();
    check_screenshot(f.screen, "466-clock");

    using core::Action;
    f.app->show(ui::App::kCountdown);
    for (int i = 0; i < 3; i++) {
        f.app->input(Action::SwipeUp, 0.25f);
    }
    f.screen.render();
    check_screenshot(f.screen, "466-countdown-set");

    f.app->show(ui::App::kAlarm);
    f.screen.render();
    check_screenshot(f.screen, "466-alarm");

    f.app->show(ui::App::kInfo);
    f.app->info()->refresh();
    f.screen.render();
    check_screenshot(f.screen, "466-info-links");
}
