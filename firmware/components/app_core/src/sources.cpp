#include "app_core/sources.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app_core/protocol.h"
#include "app_core/time_zone.h"

namespace core {
namespace {

std::string text(JsonVariantConst v)
{
    return v.is<const char *>() ? std::string(v.as<const char *>()) : std::string();
}

} // namespace

std::vector<SourceSpec> configured_sources(const Settings &settings)
{
    JsonDocument cfg;
    deserializeJson(cfg, settings.get("sources"));
    std::vector<SourceSpec> out;
    for (JsonVariantConst c : cfg.as<JsonArrayConst>()) {
        SourceSpec s{.sid = text(c["sid"]), .src = text(c["src"]), .via = text(c["via"]), .every = c["every"] | 0,
                     .params = ""};
        const bool dup = std::any_of(out.begin(), out.end(), [&](const SourceSpec &o) { return o.sid == s.sid; });
        if (!proto::valid_sid(s.sid) || s.src.empty() || dup) {
            continue;
        }
        // Parameters, with the device's location for weather and air quality.
        JsonDocument p;
        p.set(c["p"]);
        if ((s.src == "weather" || s.src == "aqi") && !p["lat"].is<double>()) {
            p["lat"] = settings.number("location.lat");
            p["lon"] = settings.number("location.lon");
        }
        if (!p.isNull()) {
            serializeJson(p, s.params);
        }
        out.push_back(std::move(s));
    }
    const bool has_geo = std::any_of(out.begin(), out.end(), [](const SourceSpec &o) { return o.sid == "geo"; });
    if (settings.flag("location.auto") && !has_geo) {
        out.push_back({.sid = "geo", .src = "geo", .via = "", .every = 0, .params = ""});
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> auto_location(const Settings &settings, const DataStore &store)
{
    const auto lat = store.number("geo.lat"), lon = store.number("geo.lon");
    if (!settings.flag("location.auto") || !lat || !lon || std::abs(*lat) > 90 || std::abs(*lon) > 180) {
        return {};
    }
    // About 100 m: the lookup only knows the city, and NVS shouldn't be written for less.
    const bool moved = std::abs(*lat - settings.number("location.lat")) > 0.001 ||
                       std::abs(*lon - settings.number("location.lon")) > 0.001;
    char buf[24];
    std::vector<std::pair<std::string, std::string>> out;
    if (moved) {
        std::snprintf(buf, sizeof buf, "%.4f", *lat);
        out.emplace_back("location.lat", buf);
        std::snprintf(buf, sizeof buf, "%.4f", *lon);
        out.emplace_back("location.lon", buf);
    }
    if (const auto city = store.text("geo.city"); city && !city->empty() && *city != settings.get("location.name")) {
        out.emplace_back("location.name", *city);
    }
    if (const auto tz = store.text("geo.tz"); tz && posix_tz(*tz) && *tz != settings.get("location.tz")) {
        out.emplace_back("location.tz", *tz);
    }
    if (!out.empty()) {
        out.emplace_back("location.auto", "true"); // keeps it on (see Settings::set_many)
    }
    return out;
}

bool device_native(std::string_view src)
{
    return src == "time" || src == "weather" || src == "aqi" || src == "http" || src == "geo";
}

bool needs_internet(std::string_view src)
{
    return src == "weather" || src == "aqi" || src == "http" || src == "geo" || src == "gitlab" || src == "calendar";
}

void store_weather(DataStore &store, JsonVariantConst m, Origin origin, uint32_t ttl_ms)
{
    const JsonVariantConst now = m["now"];
    for (const char *k : {"temp", "feels", "hum", "code", "wind", "is_day"}) {
        if (now[k].is<double>()) {
            store.put(std::string("weather.now.") + k, now[k].as<double>(), origin, ttl_ms);
        }
    }
    if (m["rain_next_2h"].is<JsonArrayConst>()) {
        double rain = 0;
        for (JsonVariantConst r : m["rain_next_2h"].as<JsonArrayConst>()) {
            rain += r | 0.0;
        }
        store.put("weather.rain_next_2h", std::round(rain * 10) / 10, origin, ttl_ms);
    }
    // The forecast: weather.day0 (today) … weather.day2.
    int i = 0;
    for (JsonVariantConst day : m["days"].as<JsonArrayConst>()) {
        if (i >= kForecastDays) {
            break;
        }
        const std::string p = "weather.day" + std::to_string(i++) + ".";
        store.put(p + "d", std::string(day["d"] | ""), origin, ttl_ms);
        for (const char *k : {"min", "max", "code", "pop"}) {
            if (day[k].is<double>()) {
                store.put(p + k, day[k].as<double>(), origin, ttl_ms);
            }
        }
        store.put(p + "sunrise", std::string(day["sunrise"] | ""), origin, ttl_ms);
        store.put(p + "sunset", std::string(day["sunset"] | ""), origin, ttl_ms);
    }
}

std::vector<std::string> store_data(DataStore &store, JsonObjectConst kv, Origin origin, uint32_t ttl_ms)
{
    std::vector<std::string> sids;
    for (JsonPairConst p : kv) {
        const std::string_view key(p.key().c_str(), p.key().size());
        const Origin o = key.substr(0, 5) == "user." ? Origin::Push : origin;
        const JsonVariantConst v = p.value();
        if (v.is<bool>()) {
            store.put(key, v.as<bool>(), o, ttl_ms);
        } else if (v.is<double>()) {
            store.put(key, v.as<double>(), o, ttl_ms);
        } else if (v.is<const char *>()) {
            store.put(key, std::string(v.as<const char *>()), o, ttl_ms);
        } else {
            continue;
        }
        if (const size_t dot = key.find('.'); dot != std::string_view::npos) {
            std::string sid(key.substr(0, dot));
            if (std::find(sids.begin(), sids.end(), sid) == sids.end()) {
                sids.push_back(std::move(sid));
            }
        }
    }
    return sids;
}

} // namespace core
