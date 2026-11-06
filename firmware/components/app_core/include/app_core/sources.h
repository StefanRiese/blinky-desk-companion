// Data sources: what the device has configured (setting "sources"), which of them the
// device can fetch itself, and how results go into the DataStore. Shared by the HostLink
// (results from a host), the DeviceFetcher (results fetched over WiFi) and the SourceRouter.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ArduinoJson.h>

#include "app_core/data_store.h"
#include "app_core/settings.h"

namespace core {

// One configured source, ready to subscribe or fetch.
struct SourceSpec {
    std::string sid, src;
    std::string via;    // auto | device | host (from the setting; empty = auto)
    int every = 0;      // seconds, 0 = the source's default
    std::string params; // JSON text (location filled in for weather and aqi), or empty

    // key changes whenever the subscription has to be renewed (interval or parameters).
    std::string key() const { return std::to_string(every) + "|" + params; }
};

// configured_sources reads the setting "sources": valid sids only, each sid once, in order.
// While location.auto is on, the location lookup by IP address (sid and src "geo") follows,
// unless a source "geo" is configured already.
std::vector<SourceSpec> configured_sources(const Settings &settings);

// What the firmware knows about a source type (src).
struct SourceInfo {
    std::string_view src;
    bool native = false;   // the device can fetch it itself over WiFi (time over NTP, the rest over HTTP)
    bool internet = false; // whoever fetches it needs internet (proxmox and ha are LAN services)
    int min_s = 30;        // shortest interval in seconds, for device-native sources
    int default_s = 300;   // interval when the source sets none
};

// source_info finds a source type; nullptr for host-only LAN services and unknown ones.
const SourceInfo *source_info(std::string_view src);
bool device_native(std::string_view src);
bool needs_internet(std::string_view src);

// json_text is a JSON string's text, or "" for anything else.
std::string json_text(JsonVariantConst v);
// coord formats a latitude or longitude with 4 decimals (about 10 m), for settings and URLs.
std::string coord(double v);

// auto_location returns the settings that store the location found by IP address (geo.* in
// the store) while location.auto is on; empty when there is none or it is stored already.
// A time zone the firmware doesn't know is left as it is.
std::vector<std::pair<std::string, std::string>> auto_location(const Settings &settings, const DataStore &store);

// kForecastDays is how many days of the forecast are kept (weather.day0 … day2).
constexpr int kForecastDays = 3;

// store_weather puts a weather result (a protocol "weather" message) into the store:
// weather.now.*, weather.rain_next_2h (sum) and the forecast as
// weather.day<i>.d/min/max/code/pop/sunrise/sunset.
void store_weather(DataStore &store, JsonVariantConst weather, Origin origin, uint32_t ttl_ms);

// store_data puts key/values of a data result into the store ("user.*" as pushed values)
// and returns the subscription ids (key prefixes) it saw.
std::vector<std::string> store_data(DataStore &store, JsonObjectConst kv, Origin origin, uint32_t ttl_ms);

} // namespace core
