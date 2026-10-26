#include <doctest/doctest.h>

#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "app_core/units.h"

using core::Origin;

namespace {
struct FakeClock {
    uint32_t ms = 1000;
    core::DataStore::NowMs fn()
    {
        return [this] { return ms; };
    }
};
} // namespace

TEST_CASE("DataStore keeps fresh higher-priority values, like the emulator")
{
    FakeClock clock;
    core::DataStore store(clock.fn());
    CHECK(store.put("indoor.temp", 21.5, Origin::Local, 60000));
    CHECK_FALSE(store.put("indoor.temp", 30.0, Origin::Host, 60000)); // local wins while fresh
    CHECK(store.number("indoor.temp") == 21.5);
    CHECK(store.put("indoor.temp", 22.0, Origin::Local, 60000)); // same origin replaces

    clock.ms += 61000; // the local value goes stale …
    CHECK_FALSE(store.number("indoor.temp").has_value());
    CHECK(store.get("indoor.temp")->stale);
    CHECK(store.put("indoor.temp", 30.0, Origin::Host, 60000)); // … so a lower origin may replace it
    CHECK(store.number("indoor.temp") == 30.0);

    CHECK(store.put("weather.now.temp", 8.4, Origin::Device, 900000));
    CHECK(store.put("weather.now.temp", 8.6, Origin::Host, 900000)); // device and host rank the same
    CHECK(store.put("user.coffee", 3.0, Origin::Push, 0));          // ttl 0: never stale
    clock.ms += 10'000'000;
    CHECK(store.number("user.coffee") == 3.0);
}

TEST_CASE("DataStore: types, prefixes, version, capacity")
{
    FakeClock clock;
    core::DataStore store(clock.fn());
    const uint32_t v0 = store.version();
    store.put("weather.now.desc", std::string("Overcast"), Origin::Host, 0);
    store.put("weather.now.temp", 8.4, Origin::Host, 0);
    store.put("indoor.hum", 44.0, Origin::Local, 0);
    CHECK(store.version() == v0 + 3);
    CHECK(store.text("weather.now.desc") == "Overcast");
    CHECK_FALSE(store.number("weather.now.desc").has_value()); // wrong type
    CHECK(store.items("weather.").size() == 2);
    CHECK(store.items("weather.")[0].first == "weather.now.desc"); // sorted
    store.remove("indoor.hum");
    CHECK_FALSE(store.get("indoor.hum").has_value());
    CHECK(store.version() == v0 + 4);

    for (size_t i = store.items().size(); i < core::DataStore::kMaxEntries; i++) {
        REQUIRE(store.put("k" + std::to_string(i), 1.0, Origin::Push, 0));
    }
    CHECK_FALSE(store.put("one.too.many", 1.0, Origin::Push, 0));
    CHECK(store.put("weather.now.temp", 9.0, Origin::Host, 0)); // existing keys still update
}

TEST_CASE("Settings: defaults, validation, secrets, persistence")
{
    core::MemoryStorage storage;
    {
        core::Settings s(storage);
        CHECK(s.number("ui.brightness_day") == 200);
        CHECK(s.get("units.temp") == "C");
        CHECK(s.flag("sound.enabled"));

        CHECK(s.set("ui.brightness_day", "300").value().find("0 to 255") != std::string::npos);
        CHECK(s.set("ui.night_from", "7.5").has_value()); // whole numbers only
        CHECK(s.set("units.temp", "K").value().find("C, F") != std::string::npos);
        CHECK(s.set("sound.enabled", "yes").has_value());
        CHECK(s.set("ui.pages", "eyes").has_value()); // JSON required
        CHECK(s.set("no.such.key", "1").has_value());

        CHECK_FALSE(s.set("units.temp", "F").has_value());
        CHECK_FALSE(s.set("wifi.pass", "geheim").has_value());
        CHECK(s.shown("wifi.pass") == "***");
        CHECK(s.get("wifi.pass") == "geheim");
        CHECK_FALSE(s.set("wifi.pass", "***").has_value()); // the mask keeps the secret
        CHECK(s.get("wifi.pass") == "geheim");
        CHECK_FALSE(s.set("location.name", "Düsseldorf\nNord").has_value()); // strings may hold anything
    }
    core::Settings again(storage); // loaded from storage
    CHECK(again.get("units.temp") == "F");
    CHECK(again.get("wifi.pass") == "geheim");
    CHECK(again.get("location.name") == "Düsseldorf\nNord");
    again.reset("units.temp");
    CHECK(again.get("units.temp") == "C");
}

TEST_CASE("Settings block encoding round-trips escapes")
{
    const std::map<std::string, std::string, std::less<>> v = {{"a", "x=y"}, {"b", "back\\slash\nnew line"}};
    CHECK(core::decode_block(core::encode_block(v)) == v);
    CHECK(core::decode_block("broken line\n=novalue\nk=v\n").size() == 1);
}

TEST_CASE("Units convert only for display")
{
    core::Units u;
    CHECK(u.temperature(21.46) == "21.5°C");
    CHECK(u.temperature(-0.2, 0) == "0°C");
    CHECK(u.wind_speed(12.4) == "12 km/h");
    CHECK(u.rain_amount(1.25) == "1.2 mm");
    u = {.temp = "F", .wind = "mph", .rain = "in"};
    CHECK(u.temperature(26) == "78.8°F");
    CHECK(u.temperature(26, 0, false) == "79°");
    CHECK(u.wind_speed(16.09) == "10 mph");
    CHECK(u.rain_amount(25.4) == "1.00 in");
    u.wind = "ms";
    CHECK(u.wind_speed(36) == "10.0 m/s");
}
