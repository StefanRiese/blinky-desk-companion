#include "app_core/device_fetcher.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace core {
namespace fetch {
namespace {

double round1(double v)
{
    return std::round(v * 10) / 10;
}

// valid_key: [a-zA-Z0-9_]+ separated by single dots, as on the host.
bool valid_key(std::string_view k)
{
    if (k.empty() || k.front() == '.' || k.back() == '.') {
        return false;
    }
    char prev = 0;
    for (char c : k) {
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!word && !(c == '.' && prev != '.')) {
            return false;
        }
        prev = c;
    }
    return true;
}

bool http_url(std::string_view url)
{
    for (std::string_view scheme : {"http://", "https://"}) {
        if (url.substr(0, scheme.size()) == scheme) {
            const std::string_view rest = url.substr(scheme.size());
            return !rest.empty() && rest.front() != '/';
        }
    }
    return false;
}

std::string coord(double v)
{
    char buf[24];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    return buf;
}

} // namespace

int min_interval(std::string_view src)
{
    if (src == "time") {
        return 60;
    }
    if (src == "weather") {
        return 300;
    }
    if (src == "aqi") {
        return 900;
    }
    return 30; // http
}

int default_interval(std::string_view src)
{
    if (src == "time") {
        return 3600;
    }
    if (src == "weather") {
        return 900;
    }
    if (src == "aqi") {
        return 1800;
    }
    return 300; // http
}

std::optional<std::string> check_params(std::string_view src, std::string_view params)
{
    JsonDocument p;
    if (!params.empty() && (deserializeJson(p, params.data(), params.size()) != DeserializationError::Ok ||
                            !p.is<JsonObject>())) {
        return "params: not a JSON object";
    }
    if (!device_native(src)) {
        return std::string(src) + " is host-only";
    }
    if (src == "weather" || src == "aqi") {
        if (!p["lat"].is<double>() || !p["lon"].is<double>()) {
            return "params: lat and lon are required";
        }
        const double lat = p["lat"], lon = p["lon"];
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
            return "params: lat/lon out of range";
        }
    }
    if (src == "http") {
        if (!http_url(p["url"] | "")) {
            return "params: url must be an absolute http(s) URL";
        }
        const JsonObjectConst pick = p["pick"];
        if (pick.isNull() || pick.size() == 0) {
            return "params: pick must name at least one value";
        }
        if (pick.size() > 16) {
            return "params: at most 16 picks";
        }
        for (JsonPairConst kv : pick) {
            const std::string_view ptr = kv.value() | "";
            if (!valid_key(kv.key().c_str())) {
                return std::string("params: invalid key \"") + kv.key().c_str() + "\"";
            }
            if (!kv.value().is<const char *>() || (!ptr.empty() && ptr.front() != '/')) {
                return std::string("params: pointer for \"") + kv.key().c_str() + "\" must start with /";
            }
        }
        for (JsonPairConst kv : p["headers"].as<JsonObjectConst>()) {
            if (!kv.value().is<const char *>()) {
                return "params: header values must be text";
            }
        }
    }
    return std::nullopt;
}

std::string weather_url(double lat, double lon)
{
    return "https://api.open-meteo.com/v1/forecast?latitude=" + coord(lat) + "&longitude=" + coord(lon) +
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
           "&minutely_15=precipitation&forecast_minutely_15=8";
}

std::string aqi_url(double lat, double lon)
{
    return "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + coord(lat) +
           "&longitude=" + coord(lon) + "&current=european_aqi,pm2_5,pm10";
}

bool weather_message(std::string_view body, JsonDocument &out)
{
    JsonDocument r;
    if (deserializeJson(r, body.data(), body.size()) != DeserializationError::Ok) {
        return false;
    }
    const JsonVariantConst cur = r["current"];
    if (!cur["temperature_2m"].is<double>()) {
        return false;
    }
    out.clear();
    out["t"] = "weather";
    JsonObject now = out["now"].to<JsonObject>();
    now["temp"] = round1(cur["temperature_2m"]);
    now["feels"] = round1(cur["apparent_temperature"] | 0.0);
    now["hum"] = round1(cur["relative_humidity_2m"] | 0.0);
    now["code"] = cur["weather_code"] | 0;
    now["wind"] = round1(cur["wind_speed_10m"] | 0.0);
    now["is_day"] = cur["is_day"] | 1;
    JsonArray rain = out["rain_next_2h"].to<JsonArray>();
    for (JsonVariantConst v : r["minutely_15"]["precipitation"].as<JsonArrayConst>()) {
        rain.add(round1(v | 0.0)); // null (no value) counts as no rain
    }
    return true;
}

bool aqi_values(std::string_view body, JsonObject out)
{
    JsonDocument r;
    if (deserializeJson(r, body.data(), body.size()) != DeserializationError::Ok ||
        !r["current"]["european_aqi"].is<double>()) {
        return false;
    }
    const JsonVariantConst cur = r["current"];
    out["index"] = static_cast<int>(cur["european_aqi"].as<double>());
    if (cur["pm2_5"].is<double>()) {
        out["pm25"] = round1(cur["pm2_5"]);
    }
    if (cur["pm10"].is<double>()) {
        out["pm10"] = round1(cur["pm10"]);
    }
    return true;
}

JsonVariantConst pointer(JsonVariantConst doc, std::string_view ptr, bool &found)
{
    found = false;
    if (!ptr.empty() && ptr.front() != '/') {
        return {};
    }
    JsonVariantConst cur = doc;
    while (!ptr.empty()) {
        ptr.remove_prefix(1); // the '/'
        const size_t end = std::min(ptr.find('/'), ptr.size());
        std::string tok;
        for (size_t i = 0; i < end; i++) {
            if (ptr[i] == '~' && i + 1 < end && (ptr[i + 1] == '0' || ptr[i + 1] == '1')) {
                tok += ptr[i + 1] == '0' ? '~' : '/';
                i++;
            } else {
                tok += ptr[i];
            }
        }
        ptr.remove_prefix(end);
        if (cur.is<JsonObjectConst>()) {
            const JsonVariantConst next = cur.as<JsonObjectConst>()[tok];
            if (next.isUnbound()) { // no such member (a member that is null is bound)
                return {};
            }
            cur = next;
            continue;
        }
        if (cur.is<JsonArrayConst>()) {
            const JsonArrayConst a = cur.as<JsonArrayConst>();
            if (tok.empty() || tok.size() > 6 || tok.find_first_not_of("0123456789") != std::string::npos) {
                return {};
            }
            const size_t i = std::stoul(tok);
            if (i >= a.size()) {
                return {};
            }
            cur = a[i];
            continue;
        }
        return {};
    }
    found = true;
    return cur;
}

std::optional<std::string> http_values(std::string_view body, JsonObjectConst pick, JsonObject out)
{
    JsonDocument doc;
    if (deserializeJson(doc, body.data(), body.size()) != DeserializationError::Ok) {
        return "response is not JSON";
    }
    std::string missing;
    int n = 0;
    for (JsonPairConst kv : pick) {
        bool found = false;
        const JsonVariantConst v = pointer(doc.as<JsonVariantConst>(), kv.value() | "", found);
        if (!found) {
            missing += (missing.empty() ? "" : ", ") + std::string(kv.key().c_str());
            continue;
        }
        n++;
        if (v.is<const char *>()) {
            out[kv.key()] = std::string(v.as<const char *>()).substr(0, 120);
        } else if (v.is<bool>() || v.is<double>() || v.isNull()) {
            out[kv.key()] = v;
        } else {
            std::string s;
            serializeJson(v, s);
            out[kv.key()] = s.substr(0, 120); // objects and lists as short JSON text
        }
    }
    if (n == 0) {
        return "no pick matched (" + missing + ")";
    }
    return std::nullopt;
}

} // namespace fetch

DeviceFetcher::DeviceFetcher(DataStore &store, DataStore::NowMs now, Hooks hooks)
    : store_(store), now_(std::move(now)), hooks_(std::move(hooks))
{
}

void DeviceFetcher::assign(const std::vector<SourceSpec> &specs)
{
    std::lock_guard lock(mu_);
    std::erase_if(jobs_, [&](const auto &kv) {
        return std::none_of(specs.begin(), specs.end(), [&](const SourceSpec &s) { return s.sid == kv.first; });
    });
    for (const SourceSpec &s : specs) {
        if (auto it = jobs_.find(s.sid); it != jobs_.end() && it->second.spec.key() == s.key() &&
                                         it->second.spec.src == s.src) {
            continue;
        }
        int every = s.every > 0 ? s.every : fetch::default_interval(s.src);
        every = std::max(every, fetch::min_interval(s.src));
        Job j{.spec = s, .status = {.state = "pending", .msg = "", .every = every}, .gen = ++gen_};
        if (auto err = fetch::check_params(s.src, s.params)) {
            j.bad = true;
            j.status.state = "error";
            j.status.msg = *err;
        }
        jobs_[s.sid] = std::move(j);
    }
}

void DeviceFetcher::retry()
{
    std::lock_guard lock(mu_);
    for (auto &[sid, j] : jobs_) {
        if (j.status.state == "error" && !j.bad) {
            j.now = true;
        }
    }
}

std::optional<DeviceFetcher::Status> DeviceFetcher::status(std::string_view sid) const
{
    std::lock_guard lock(mu_);
    if (auto it = jobs_.find(sid); it != jobs_.end()) {
        return it->second.status;
    }
    return std::nullopt;
}

uint32_t DeviceFetcher::run_due()
{
    for (;;) {
        SourceSpec spec;
        uint32_t gen = 0;
        int every = 0;
        uint32_t next = UINT32_MAX;
        {
            std::lock_guard lock(mu_);
            const uint32_t now = now_();
            for (auto &[sid, j] : jobs_) {
                if (j.bad) {
                    continue;
                }
                const int32_t left = static_cast<int32_t>(j.due - now);
                if (j.now || left <= 0) {
                    spec = j.spec;
                    gen = j.gen;
                    every = j.status.every;
                    j.now = false;
                    break;
                }
                next = std::min(next, static_cast<uint32_t>(left));
            }
            if (gen == 0) {
                return next;
            }
        }
        const auto err = run(spec, every);
        std::lock_guard lock(mu_);
        const auto it = jobs_.find(spec.sid);
        if (it == jobs_.end() || it->second.gen != gen) {
            continue; // replaced or removed meanwhile
        }
        Job &j = it->second;
        const uint32_t every_ms = static_cast<uint32_t>(every) * 1000u;
        j.due = now_() + (err ? std::min(every_ms, kRetryMs) : every_ms);
        j.status.state = err ? "error" : "ok";
        j.status.msg = err.value_or("");
    }
}

std::optional<std::string> DeviceFetcher::run(const SourceSpec &spec, int every)
{
    const uint32_t ttl = static_cast<uint32_t>(every) * 3000u; // three intervals, as in the emulator
    if (spec.src == "time") {
        const auto epoch = hooks_.sync_time ? hooks_.sync_time() : std::nullopt;
        if (!epoch) {
            return "no answer from the NTP server";
        }
        if (hooks_.set_time) {
            hooks_.set_time(*epoch);
        }
        return std::nullopt;
    }

    JsonDocument p;
    deserializeJson(p, spec.params);
    std::string url;
    fetch::Headers headers;
    if (spec.src == "weather") {
        url = fetch::weather_url(p["lat"], p["lon"]);
    } else if (spec.src == "aqi") {
        url = fetch::aqi_url(p["lat"], p["lon"]);
    } else {
        url = p["url"] | "";
        for (JsonPairConst kv : p["headers"].as<JsonObjectConst>()) {
            headers.emplace_back(kv.key().c_str(), kv.value() | "");
        }
    }
    if (!hooks_.get) {
        return "no HTTP client";
    }
    const fetch::Response r = hooks_.get(url, headers);
    if (r.status == 0) {
        return r.error.empty() ? "request failed" : r.error;
    }
    if (r.status != 200) {
        return "HTTP " + std::to_string(r.status);
    }

    if (spec.src == "weather") {
        JsonDocument w;
        if (!fetch::weather_message(r.body, w)) {
            return "unexpected answer from Open-Meteo";
        }
        store_weather(store_, w.as<JsonVariantConst>(), Origin::Device, ttl);
        return std::nullopt;
    }
    JsonDocument values;
    JsonObject out = values.to<JsonObject>();
    if (spec.src == "aqi") {
        if (!fetch::aqi_values(r.body, out)) {
            return "unexpected answer from Open-Meteo";
        }
    } else if (auto err = fetch::http_values(r.body, p["pick"].as<JsonObjectConst>(), out)) {
        return err;
    }
    JsonDocument kv;
    for (JsonPair v : out) {
        kv[spec.sid + "." + v.key().c_str()] = v.value();
    }
    store_data(store_, kv.as<JsonObjectConst>(), Origin::Device, ttl);
    return std::nullopt;
}

} // namespace core
