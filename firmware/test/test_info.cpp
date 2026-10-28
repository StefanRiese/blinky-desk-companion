#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "fixture.h"
#include "ui/icons.h"
#include "ui/theme.h"

namespace {

uint32_t hex(lv_color_t c)
{
    return lv_color_to_u32(c) & 0xFFFFFF;
}

// texts collects the text of every label under obj, in order.
void texts(lv_obj_t *obj, std::string &out)
{
    if (lv_obj_check_type(obj, &lv_label_class) && !lv_obj_is_hidden(obj)) {
        out += lv_label_get_text(obj);
        out += '\n';
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        lv_obj_t *child = lv_obj_get_child(obj, static_cast<int32_t>(i));
        if (!lv_obj_is_hidden(child)) {
            texts(child, out);
        }
    }
}

std::string screen_text(AppFixture &f, ui::App::Screen s)
{
    std::string out;
    texts(f.app->screen(s), out);
    return out;
}

bool has(const std::string &all, const std::string &line)
{
    return all.find(line + "\n") != std::string::npos;
}

} // namespace

TEST_CASE("info screen: links and sources")
{
    AppFixture f;
    f.app->show(ui::App::kInfo);
    f.screen.render();
    CHECK(f.screen.pixel(0, 0) == hex(ui::theme::background())); // corner: outside the circle
    CHECK(f.screen.pixel(206, 2) == hex(ui::theme::rim()));       // rim ring at the top

    std::string t = screen_text(f, ui::App::kInfo);
    CHECK(has(t, "WiFi not set up"));
    CHECK(has(t, "no host on USB"));
    CHECK(has(t, "no TCP host set"));
    CHECK(t.find("·  device") == std::string::npos); // no sources yet

    f.status = sim::demo_status();
    f.app->info()->refresh();
    t = screen_text(f, ui::App::kInfo);
    CHECK(has(t, "Home  ·  192.168.1.23"));
    CHECK(has(t, "buddy-link@laptop"));
    CHECK(has(t, "time  ·  device"));
    CHECK(has(t, "aqi  ·  HTTP 503"));
    CHECK(has(t, "sys  ·  usb"));

    // More sources than rows: the last row sums up the rest.
    for (const char *sid : {"a", "b"}) {
        f.status.sources.push_back({.sid = sid, .src = "http", .via = "device", .link = "", .state = "ok", .msg = "",
                                    .every = 0});
    }
    f.app->info()->refresh();
    t = screen_text(f, ui::App::kInfo);
    CHECK(has(t, "+ 3 more sources"));
    CHECK_FALSE(has(t, "ci  ·  gitlab is host-only, host doesn't offer gitlab"));
}

TEST_CASE("touch tester counts taps")
{
    AppFixture f;
    f.app->show(ui::App::kInfo);
    CHECK(f.app->info()->touches() == 0);
    f.screen.touch(300, 230, true);
    f.screen.touch(300, 230, false);
    CHECK(f.app->info()->touches() == 1);
    f.screen.touch(120, 300, true);
    f.screen.touch(120, 300, false);
    CHECK(f.app->info()->touches() == 2);
}

TEST_CASE("status bar: time, temperature, stale marker, link icons")
{
    AppFixture f;
    f.app->tick();
    std::string t = screen_text(f, ui::App::kEyes);
    CHECK_FALSE(has(t, "07:16")); // the clock isn't set, there is no weather, nothing connected

    f.time_set = true;
    f.store.put("weather.now.temp", 8.4, core::Origin::Host, 2000);
    f.status = sim::demo_status();
    f.app->show(ui::App::kEyes); // refreshes the bar
    t = screen_text(f, ui::App::kEyes);
    CHECK(has(t, "07:16"));
    CHECK(has(t, "8°C"));
    CHECK(t.find(ui::icon::kWarning) == std::string::npos);
    CHECK(t.find(ui::icon::kWifi) != std::string::npos);
    CHECK(t.find(ui::icon::kUsb) != std::string::npos);
    CHECK(t.find(ui::icon::kNetwork) == std::string::npos); // TCP not connected

    // The dashboard shows only the icons; the temperature goes stale after its TTL.
    f.app->show(ui::App::kDashboard);
    t = screen_text(f, ui::App::kDashboard);
    CHECK(t.find(ui::icon::kWarning) == std::string::npos);
    CHECK(std::count(t.begin(), t.end(), '\n') > 3);
    f.store.put("weather.now.temp", 8.4, core::Origin::Host, 1);
    lv_delay_ms(5);
    f.app->show(ui::App::kCountdown);
    t = screen_text(f, ui::App::kCountdown);
    CHECK(has(t, "8°C"));
    CHECK(t.find(ui::icon::kWarning) != std::string::npos);

    // Battery: nothing while it is fine, the low sign with the percentage, charging.
    CHECK(t.find(ui::icon::kBatteryLow) == std::string::npos);
    f.status.battery = 64;
    f.app->show(ui::App::kEyes);
    t = screen_text(f, ui::App::kEyes);
    CHECK(t.find(ui::icon::kBatteryLow) == std::string::npos);
    CHECK_FALSE(has(t, "64 %"));
    f.status.battery = 12;
    f.app->show(ui::App::kEyes);
    t = screen_text(f, ui::App::kEyes);
    CHECK(t.find(ui::icon::kBatteryLow) != std::string::npos);
    CHECK(has(t, "12 %"));
    f.status.charging = true;
    f.app->show(ui::App::kEyes);
    t = screen_text(f, ui::App::kEyes);
    CHECK(t.find(ui::icon::kBatteryCharging) != std::string::npos);
    CHECK_FALSE(has(t, "12 %"));
}
