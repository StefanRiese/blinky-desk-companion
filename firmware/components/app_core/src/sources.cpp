#include "app_core/sources.h"

#include <algorithm>
#include <cmath>

#include "app_core/protocol.h"

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
        SourceSpec s{.sid = text(c["sid"]), .src = text(c["src"]), .via = text(c["via"]), .every = c["every"] | 0};
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
    return out;
}

bool device_native(std::string_view src)
{
    return src == "time" || src == "weather" || src == "aqi" || src == "http";
}

bool needs_internet(std::string_view src)
{
    return src == "weather" || src == "aqi" || src == "http" || src == "gitlab" || src == "calendar";
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
