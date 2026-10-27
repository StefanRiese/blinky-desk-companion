// MoodEngine, backlight and sound (levels, reactions, beeps), and how the app uses them.
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>
#include <vector>

#include "app_core/backlight.h"
#include "app_core/sound.h"
#include "fixture.h"
#include "mood/mood_engine.h"

namespace {

std::tm at(int hour)
{
    std::tm t{};
    t.tm_hour = hour;
    return t;
}

struct Engine {
    uint32_t ms = 1000;
    core::MemoryStorage storage;
    core::Settings settings{storage};
    core::DataStore store{[this] { return ms; }};
    mood::MoodEngine engine{settings, store};

    mood::Mood eval(mood::Context ctx = {}) { return engine.evaluate(ctx, ms); }
};

} // namespace

TEST_CASE("MoodEngine: the first matching rule wins")
{
    Engine e;
    CHECK(e.eval() == mood::Mood{"neutral", "idle"});

    e.store.put("weather.now.code", 0.0, core::Origin::Host, 60000);
    e.store.put("weather.now.is_day", 1.0, core::Origin::Host, 60000);
    CHECK(e.eval() == mood::Mood{"happy", "sunny"});
    e.store.put("weather.now.is_day", 0.0, core::Origin::Host, 60000);
    CHECK(e.eval().name == "neutral"); // clear, but night outside

    mood::Context ctx;
    ctx.host_lost = true;
    CHECK(e.eval(ctx) == mood::Mood{"sad", "host link lost"});
    const std::tm night = at(23);
    ctx.local = &night;
    CHECK(e.eval(ctx) == mood::Mood{"sleepy", "night"});

    e.store.put("weather.now.code", 61.0, core::Origin::Host, 60000);
    CHECK(e.eval(ctx) == mood::Mood{"sad", "rain outside"});
    e.store.put("weather.now.code", 73.0, core::Origin::Host, 60000);
    CHECK(e.eval(ctx) == mood::Mood{"cold", "snow outside"});

    e.store.put("indoor.noise", 74.0, core::Origin::Local, 10000);
    CHECK(e.eval(ctx) == mood::Mood{"angry", "too loud (74 dB)"});
    e.settings.set("sound.enabled", "false");
    CHECK(e.eval(ctx).name == "cold");
    e.settings.set("sound.enabled", "true");

    e.store.put("indoor.hum", 25.0, core::Origin::Local, 60000);
    CHECK(e.eval(ctx) == mood::Mood{"dry", "humidity 25 %"});
    e.store.put("indoor.temp", 17.2, core::Origin::Local, 60000);
    CHECK(e.eval(ctx) == mood::Mood{"cold", "indoor 17.2°C"});
    e.settings.set("units.temp", "F");
    e.store.put("indoor.temp", 27.0, core::Origin::Local, 60000);
    CHECK(e.eval(ctx) == mood::Mood{"hot", "indoor 80.6°F"});

    e.engine.set_override("love", 5000, e.ms);
    CHECK(e.eval(ctx) == mood::Mood{"love", "host"});
    ctx.countdown_ringing = true;
    CHECK(e.eval(ctx) == mood::Mood{"surprised", "countdown done"});
    ctx.alarm_ringing = true;
    CHECK(e.eval(ctx) == mood::Mood{"surprised", "alarm"});
    e.engine.react("dizzy", "shaken", 3000, e.ms);
    CHECK(e.eval(ctx) == mood::Mood{"dizzy", "shaken"});

    // Reactions and overrides run out; stale data no longer counts.
    e.ms += 3000;
    CHECK(e.eval(ctx).reason == "alarm");
    ctx = {};
    CHECK(e.eval(ctx).reason == "host");
    e.ms += 2000;
    CHECK(e.eval(ctx).name == "hot");
    e.ms += 60000;
    CHECK(e.eval(ctx) == mood::Mood{"neutral", "idle"});
}

TEST_CASE("backlight: day, night, dimmed")
{
    core::MemoryStorage storage;
    core::Settings s(storage);
    const std::tm day = at(12), late = at(23), early = at(6), morning = at(7);
    CHECK(core::backlight(s, nullptr, 0) == core::BacklightLevel{200, "day"}); // clock not set
    CHECK(core::backlight(s, &day, 59999) == core::BacklightLevel{200, "day"});
    CHECK(core::backlight(s, &day, 60000) == core::BacklightLevel{80, "dim_day"});
    CHECK(core::backlight(s, &late, 0) == core::BacklightLevel{40, "night"});
    CHECK(core::backlight(s, &early, 60000) == core::BacklightLevel{10, "dim_night"});
    CHECK_FALSE(core::is_night(s, morning));
    s.set("ui.dim_after", "0"); // never dims
    CHECK(core::backlight(s, &day, 3600000).mode == "day");
    s.set("ui.night_from", "1"); // night within one day: 01–07
    CHECK(core::is_night(s, early));
    CHECK_FALSE(core::is_night(s, late));
}

TEST_CASE("app: the backlight dims without touches and wakes on one")
{
    AppFixture f;
    f.settings.set("ui.dim_after", "1");
    REQUIRE(f.backlight == std::vector<int>{200});
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    f.app->tick();
    CHECK(f.backlight.back() == 80);
    f.screen.touch(206, 206, true);
    f.screen.touch(206, 206, false);
    CHECK(f.backlight.back() == 200);
    CHECK(f.app->backlight() == 200);
}

TEST_CASE("SoundLevel: startle, voices, too loud, baseline")
{
    core::SoundLevel s;
    uint32_t t = 0;
    auto feed = [&](double db, int n) {
        std::vector<core::SoundLevel::Event> ev;
        for (int i = 0; i < n; i++, t += core::SoundLevel::kTickMs) {
            if (auto e = s.add(db, t, 80); e != core::SoundLevel::Event::None) {
                ev.push_back(e);
            }
        }
        return ev;
    };
    CHECK(feed(40, 100).empty()); // a quiet room
    CHECK(s.base() == doctest::Approx(40));

    using E = core::SoundLevel::Event;
    CHECK(feed(92, 1) == std::vector<E>{E::Loud}); // a clap
    CHECK(feed(92, 1).empty());                    // cooling down
    feed(40, 40);
    CHECK(feed(54, 7).empty()); // voices, but only for 0.7 s
    CHECK(feed(54, 3) == std::vector<E>{E::Voices});

    // Lasting noise: the average climbs, the room level follows within a few minutes.
    feed(75, 600);
    CHECK(s.average() > 70);
    feed(75, 1800);
    CHECK(s.base() > 70);
    CHECK(feed(92, 1).empty()); // not 20 dB above the (now loud) room
}

TEST_CASE("sound: dB from samples, beeps")
{
    std::vector<int32_t> sine(1600);
    for (size_t i = 0; i < sine.size(); i++) { // full scale 1 kHz at 16 kHz, plus a DC offset
        sine[i] = static_cast<int32_t>(2147483000.0 / 2 * std::sin(2 * std::numbers::pi * 1000 * i / 16000.0)) + 1000;
    }
    CHECK(core::level_db(sine) == doctest::Approx(120 - 6 - 3).epsilon(0.01)); // half scale RMS
    CHECK(core::level_db(std::vector<int32_t>(100, 5)) == 0);

    CHECK(core::tone_hz(core::Tune::Alarm, 0) == 880);
    CHECK(core::tone_hz(core::Tune::Alarm, 200) == 0);
    CHECK(core::tone_hz(core::Tune::Alarm, 1300) == 880); // repeats
    CHECK(core::tone_hz(core::Tune::Countdown, 450) == 1319);
    CHECK(core::tone_hz(core::Tune::None, 0) == 0);

    std::vector<int32_t> out(24000); // one second of the alarm at 24 kHz
    core::synth(core::Tune::Alarm, 0, 24000, 0.5, out);
    CHECK(out[0] == 0);                                      // fades in
    CHECK(std::abs(out[24 * 200 + 10]) == 0);                // the pause
    int32_t peak = 0;
    for (size_t i = 24 * 20; i < 24 * 140; i++) {
        peak = std::max(peak, std::abs(out[i]));
    }
    CHECK(peak == doctest::Approx(0.5 * 2147483647.0).epsilon(0.01));
}
