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

// device_native: the device can fetch it itself over WiFi (time over NTP, the rest over HTTP).
bool device_native(std::string_view src);
// needs_internet: whoever fetches it needs internet (proxmox and ha are LAN services).
bool needs_internet(std::string_view src);

// auto_location returns the settings that store the location found by IP address (geo.* in
// the store) while location.auto is on; empty when there is none or it is stored already.
// A time zone the firmware doesn't know is left as it is.
std::vector<std::pair<std::string, std::string>> auto_location(const Settings &settings, const DataStore &store);

// store_weather puts a weather result (a protocol "weather" message) into the store.
void store_weather(DataStore &store, JsonVariantConst weather, Origin origin, uint32_t ttl_ms);

// store_data puts key/values of a data result into the store ("user.*" as pushed values)
// and returns the subscription ids (key prefixes) it saw.
std::vector<std::string> store_data(DataStore &store, JsonObjectConst kv, Origin origin, uint32_t ttl_ms);

} // namespace core
