// DeviceFetcher: fetches the sources the SourceRouter gives to the device itself, over its
// own WiFi (the StandaloneFetcher of the plan): time over NTP, weather and air quality from
// Open-Meteo, the location by IP address (geo) and http (any JSON URL, values picked with
// JSON Pointers). Same parameters,
// intervals and results as the host's sources (host/internal/source), so a source gives the
// same DataStore keys whoever fetches it.
//
// HTTP and NTP are hooks (esp_http_client and SNTP on the device, fakes in the tests).
// assign() and status() are thread-safe; run_due() runs in the network task and blocks
// while it fetches.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ArduinoJson.h>

#include "app_core/data_store.h"
#include "app_core/sources.h"

namespace core {

namespace fetch {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Response {
    int status = 0;     // HTTP status, 0 if the request failed
    std::string body;
    std::string error;  // why the request failed (DNS, TLS, timeout, too large, …)
};

constexpr size_t kMaxBody = 16 * 1024;

// min_interval and default_interval in seconds, as on the host.
int min_interval(std::string_view src);
int default_interval(std::string_view src);

// check_params validates a source's parameters (JSON text); returns the error or nullopt.
std::optional<std::string> check_params(std::string_view src, std::string_view params);

std::string weather_url(double lat, double lon);
std::string aqi_url(double lat, double lon);
constexpr const char *kGeoUrl = "https://ipinfo.io/json";

// weather_message turns an Open-Meteo forecast into a protocol "weather" message.
bool weather_message(std::string_view body, JsonDocument &out);
// aqi_values turns an Open-Meteo air-quality answer into data values (index, pm25, pm10).
bool aqi_values(std::string_view body, JsonObject out);
// geo_values turns an ipinfo.io answer into data values (lat, lon, city, tz).
bool geo_values(std::string_view body, JsonObject out);
// http_values picks values from a JSON body with JSON Pointers; returns the error (no pick
// matched, not JSON) or nullopt.
std::optional<std::string> http_values(std::string_view body, JsonObjectConst pick, JsonObject out);

// pointer resolves a JSON Pointer (RFC 6901); found tells whether it exists.
JsonVariantConst pointer(JsonVariantConst doc, std::string_view ptr, bool &found);

} // namespace fetch

class DeviceFetcher {
public:
    static constexpr uint32_t kRetryMs = 30000; // after an error, but not later than the interval

    struct Hooks {
        std::function<fetch::Response(const std::string &url, const fetch::Headers &headers)> get;
        std::function<std::optional<int64_t>()> sync_time; // NTP; the epoch, or nullopt
        std::function<void(int64_t epoch)> set_time;
    };

    struct Status {
        std::string state; // pending | ok | error
        std::string msg;
        int every = 0; // effective interval in seconds
    };

    DeviceFetcher(DataStore &store, DataStore::NowMs now, Hooks hooks);

    // assign sets the sources to fetch: new and changed ones are fetched right away.
    void assign(const std::vector<SourceSpec> &specs);
    // retry fetches the sources that failed now (e.g. when the internet is back).
    void retry();
    // run_due fetches every source that is due; returns the ms until the next one is due.
    uint32_t run_due();

    std::optional<Status> status(std::string_view sid) const;

private:
    struct Job {
        SourceSpec spec;
        Status status;
        uint32_t due = 0;
        bool now = true;  // due right away
        bool bad = false; // invalid parameters: never fetched
        uint32_t gen = 0; // changes when the job is replaced
    };

    std::optional<std::string> run(const SourceSpec &spec, int every);

    DataStore &store_;
    DataStore::NowMs now_;
    Hooks hooks_;
    mutable std::mutex mu_;
    std::map<std::string, Job, std::less<>> jobs_;
    uint32_t gen_ = 0;
};

} // namespace core
