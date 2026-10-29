// Demo data for the simulator and screenshots: a cloudy autumn morning, the buddy on WiFi
// with a laptop on USB.
#pragma once

#include <ctime>
#include <string>

#include "app_core/data_store.h"
#include "app_core/status.h"

namespace sim {

inline void fill_demo(core::DataStore &store)
{
    using core::Origin;
    store.put("weather.now.temp", 8.4, Origin::Host, 0);
    store.put("weather.now.code", 3.0, Origin::Host, 0);
    store.put("weather.now.is_day", 1.0, Origin::Host, 0);
    store.put("weather.now.hum", 82.0, Origin::Host, 0);
    store.put("weather.now.wind", 14.0, Origin::Host, 0);
    store.put("weather.rain_next_2h", 0.4, Origin::Host, 0);
    store.put("indoor.temp", 21.6, Origin::Local, 0);
    store.put("indoor.hum", 44.0, Origin::Local, 0);
    store.put("indoor.noise", 38.0, Origin::Local, 0);
    // The forecast: today and the next two days.
    const struct {
        const char *d;
        double min, max, code, pop;
        const char *rise, *set;
    } days[] = {{"2026-10-26", 6, 13, 61, 80, "07:12", "17:21"},
                {"2026-10-27", 5, 12, 3, 30, "07:14", "17:19"},
                {"2026-10-28", 4, 14, 1, 10, "07:16", "17:17"}};
    for (int i = 0; i < 3; i++) {
        const std::string p = "weather.day" + std::to_string(i) + ".";
        store.put(p + "d", std::string(days[i].d), Origin::Host, 0);
        store.put(p + "min", days[i].min, Origin::Host, 0);
        store.put(p + "max", days[i].max, Origin::Host, 0);
        store.put(p + "code", days[i].code, Origin::Host, 0);
        store.put(p + "pop", days[i].pop, Origin::Host, 0);
        store.put(p + "sunrise", std::string(days[i].rise), Origin::Host, 0);
        store.put(p + "sunset", std::string(days[i].set), Origin::Host, 0);
    }
    // Values from the laptop and a script.
    store.put("sys.cpu", 23.0, Origin::Host, 0);
    store.put("sys.ram", 41.5, Origin::Host, 0);
    store.put("ci.main", std::string("passed"), Origin::Host, 0);
    store.put("user.coffee", 3.0, Origin::Push, 0);
}

// demo_status: WiFi with internet, buddy-link on USB, a TCP host that isn't reachable, and
// sources in every state.
inline core::Status demo_status()
{
    core::Status s;
    s.wifi = core::Status::Wifi::Online;
    s.wifi_text = "Home  ·  192.168.1.23";
    s.usb = {.up = true, .text = "buddy-link@laptop"};
    s.tcp = {.up = false, .text = "connecting to pve.lan:7531"};
    auto src = [](const char *sid, const char *via, const char *link, const char *state, const char *msg) {
        return core::SourceRouter::Status{
            .sid = sid, .src = sid, .via = via, .link = link, .state = state, .msg = msg, .every = 0};
    };
    s.sources = {src("time", "device", "", "ok", ""), src("weather", "device", "", "ok", ""),
                 src("aqi", "device", "", "error", "HTTP 503"), src("sys", "host", "usb", "ok", ""),
                 src("ci", "none", "", "unavailable", "gitlab is host-only, host doesn't offer gitlab")};
    return s;
}

// fixed_time is Monday, 26 October 2026, 07:16 (for reproducible screenshots).
inline bool fixed_time(std::tm &t)
{
    t = {};
    t.tm_year = 126;
    t.tm_mon = 9;
    t.tm_mday = 26;
    t.tm_wday = 1;
    t.tm_hour = 7;
    t.tm_min = 16;
    return true;
}

} // namespace sim
