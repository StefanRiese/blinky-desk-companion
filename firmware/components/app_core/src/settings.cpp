#include "app_core/settings.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app_core/pages.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 2> kTempUnits = {"C", "F"};
constexpr std::array<std::string_view, 3> kWindUnits = {"kmh", "mph", "ms"};
constexpr std::array<std::string_view, 2> kRainUnits = {"mm", "in"};

// Keep in sync with DefaultNVS in host/internal/sim/nvs.go and the setup page (site/).
constexpr std::array kRegistry = {
    Setting{.key = "dev", .type = Type::String, .def = "buddy"},
    Setting{.key = "wifi.ssid", .type = Type::String, .def = ""},
    Setting{.key = "wifi.pass", .type = Type::String, .def = "", .secret = true},
    Setting{.key = "location.name", .type = Type::String, .def = "Home"},
    Setting{.key = "location.lat", .type = Type::Float, .def = "51.23", .min = -90, .max = 90},
    Setting{.key = "location.lon", .type = Type::Float, .def = "6.78", .min = -180, .max = 180},
    Setting{.key = "location.tz", .type = Type::String, .def = "Europe/Berlin"},
    Setting{.key = "location.auto", .type = Type::Bool, .def = "true"},
    Setting{.key = "link.tcp_host", .type = Type::String, .def = ""},
    Setting{.key = "link.token", .type = Type::String, .def = "", .secret = true},
    Setting{.key = "sources", .type = Type::Json,
            .def = R"([{"sid":"time","src":"time","every":3600},{"sid":"weather","src":"weather","every":900},)"
                   R"({"sid":"aqi","src":"aqi","every":1800}])"},
    Setting{.key = "ui.brightness_day", .type = Type::Int, .def = "200", .min = 0, .max = 255},
    Setting{.key = "ui.brightness_night", .type = Type::Int, .def = "40", .min = 0, .max = 255},
    Setting{.key = "ui.dim_day", .type = Type::Int, .def = "80", .min = 0, .max = 255},
    Setting{.key = "ui.dim_night", .type = Type::Int, .def = "10", .min = 0, .max = 255},
    Setting{.key = "ui.dim_after", .type = Type::Int, .def = "60", .min = 0, .max = 3600},
    Setting{.key = "ui.night_from", .type = Type::Int, .def = "22", .min = 0, .max = 23},
    Setting{.key = "ui.night_to", .type = Type::Int, .def = "7", .min = 0, .max = 23},
    Setting{.key = "ui.pages", .type = Type::Json,
            .def = R"(["eyes","dashboard","clock","forecast","indoor","host","countdown","alarm"])"},
    Setting{.key = "units.temp", .type = Type::Enum, .def = "C", .choices = kTempUnits},
    Setting{.key = "units.wind", .type = Type::Enum, .def = "kmh", .choices = kWindUnits},
    Setting{.key = "units.rain", .type = Type::Enum, .def = "mm", .choices = kRainUnits},
    Setting{.key = "mood.hot", .type = Type::Float, .def = "26", .min = -20, .max = 50},
    Setting{.key = "mood.cold", .type = Type::Float, .def = "18", .min = -20, .max = 50},
    Setting{.key = "mood.dry", .type = Type::Float, .def = "30", .min = 0, .max = 100},
    Setting{.key = "alarm.list", .type = Type::Json,
            .def = R"([{"at":"07:00","days":["mon","tue","wed","thu","fri"],"on":false},)"
                   R"({"at":"09:00","days":["sat","sun"],"on":false}])"},
    Setting{.key = "alarm.snooze_min", .type = Type::Int, .def = "9", .min = 1, .max = 30},
    Setting{.key = "sound.enabled", .type = Type::Bool, .def = "true"},
    Setting{.key = "sound.loud", .type = Type::Int, .def = "80", .min = 50, .max = 110},
    Setting{.key = "sound.too_loud", .type = Type::Int, .def = "70", .min = 40, .max = 100},
    Setting{.key = "sensor.temp_offset", .type = Type::Float, .def = "0", .min = -10, .max = 10},
    Setting{.key = "sensor.hum_offset", .type = Type::Float, .def = "0", .min = -20, .max = 20},
};

// A location set by hand turns location.auto off (see Settings::set_many).
constexpr std::array<std::string_view, 4> kLocationKeys = {"location.name", "location.lat", "location.lon",
                                                           "location.tz"};

std::optional<double> parse_number(std::string_view v)
{
    const std::string s(v);
    if (s.empty()) {
        return std::nullopt;
    }
    char *end = nullptr;
    errno = 0;
    const double d = std::strtod(s.c_str(), &end);
    if (errno != 0 || end != s.c_str() + s.size() || !std::isfinite(d)) {
        return std::nullopt;
    }
    return d;
}

} // namespace

std::span<const Setting> registry()
{
    return kRegistry;
}

const Setting *find_setting(std::string_view key)
{
    for (const auto &s : kRegistry) {
        if (s.key == key) {
            return &s;
        }
    }
    return nullptr;
}

std::string encode_block(const std::map<std::string, std::string, std::less<>> &values)
{
    std::string out;
    for (const auto &[k, v] : values) {
        out += k;
        out += '=';
        for (char c : v) {
            if (c == '\\') {
                out += "\\\\";
            } else if (c == '\n') {
                out += "\\n";
            } else {
                out += c;
            }
        }
        out += '\n';
    }
    return out;
}

std::map<std::string, std::string, std::less<>> decode_block(std::string_view block)
{
    std::map<std::string, std::string, std::less<>> out;
    while (!block.empty()) {
        const size_t nl = block.find('\n');
        const std::string_view line = block.substr(0, nl);
        block = nl == std::string_view::npos ? std::string_view{} : block.substr(nl + 1);
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) {
            continue;
        }
        std::string v;
        for (size_t i = eq + 1; i < line.size(); i++) {
            if (line[i] == '\\' && i + 1 < line.size()) {
                v += line[i + 1] == 'n' ? '\n' : line[i + 1];
                i++;
            } else {
                v += line[i];
            }
        }
        out.emplace(std::string(line.substr(0, eq)), std::move(v));
    }
    return out;
}

Settings::Settings(Storage &storage) : storage_(storage)
{
    if (const auto block = storage_.load()) {
        values_ = decode_block(*block);
    }
    // Saved before location.auto existed: a location set by hand stays as it is.
    const bool set_by_hand = std::any_of(kLocationKeys.begin(), kLocationKeys.end(),
                                         [&](std::string_view k) { return values_.contains(k); });
    if (set_by_hand && !values_.contains("location.auto")) {
        values_["location.auto"] = "false";
    }
}

std::string Settings::get(std::string_view key) const
{
    std::lock_guard lock(mu_);
    return get_locked(key);
}

std::string Settings::get_locked(std::string_view key) const
{
    if (auto it = values_.find(key); it != values_.end()) {
        return it->second;
    }
    const Setting *s = find_setting(key);
    return s ? std::string(s->def) : std::string();
}

double Settings::number(std::string_view key) const
{
    return parse_number(get(key)).value_or(0);
}

bool Settings::flag(std::string_view key) const
{
    return get(key) == "true";
}

std::string Settings::shown(std::string_view key) const
{
    const Setting *s = find_setting(key);
    std::string v = get(key);
    if (s && s->secret && !v.empty()) {
        return std::string(kMasked);
    }
    return v;
}

std::optional<std::string> Settings::validate(const Setting &s, std::string_view value) const
{
    const std::string key(s.key);
    switch (s.type) {
    case Type::Bool:
        if (value != "true" && value != "false") {
            return key + " must be true or false";
        }
        break;
    case Type::Int:
    case Type::Float: {
        const auto d = parse_number(value);
        if (!d || (s.type == Type::Int && *d != std::floor(*d))) {
            return key + (s.type == Type::Int ? " must be a whole number" : " must be a number");
        }
        if (s.min != s.max && (*d < s.min || *d > s.max)) {
            char buf[96];
            std::snprintf(buf, sizeof buf, " must be from %g to %g", s.min, s.max);
            return key + buf;
        }
        break;
    }
    case Type::Enum: {
        bool ok = false;
        std::string all;
        for (auto c : s.choices) {
            ok = ok || c == value;
            all += (all.empty() ? "" : ", ") + std::string(c);
        }
        if (!ok) {
            return key + " must be one of " + all;
        }
        break;
    }
    case Type::Json:
        if (value.empty() || (value.front() != '[' && value.front() != '{')) {
            return key + " must be JSON (a list or an object)";
        }
        if (key == "ui.pages") {
            return check_pages(value);
        }
        break;
    case Type::String:
        break;
    }
    if (value.find('\n') != std::string_view::npos && s.type != Type::Json && s.type != Type::String) {
        return key + " must be on one line";
    }
    return std::nullopt;
}

// check validates one value; keep is set when a secret came back masked (store nothing).
std::optional<std::string> Settings::check(std::string_view key, std::string_view value, bool &keep) const
{
    keep = false;
    const Setting *s = find_setting(key);
    if (s == nullptr) {
        return "unknown setting " + std::string(key);
    }
    if (s->secret && value == kMasked) {
        keep = true; // the editor sent back the mask: keep the stored secret
        return std::nullopt;
    }
    return validate(*s, value);
}

std::optional<std::string> Settings::set(std::string_view key, std::string_view value)
{
    return set_many({{std::string(key), std::string(value)}});
}

std::optional<std::string> Settings::set_many(const std::vector<std::pair<std::string, std::string>> &values)
{
    std::vector<const std::pair<std::string, std::string> *> store;
    for (const auto &kv : values) {
        bool keep = false;
        if (auto err = check(kv.first, kv.second, keep)) {
            return err;
        }
        if (!keep) {
            store.push_back(&kv);
        }
    }
    if (store.empty()) {
        return std::nullopt;
    }
    std::lock_guard lock(mu_);
    // Changing the location by hand ends the lookup by IP address, unless location.auto is
    // set in the same call (the found location is stored that way).
    bool by_hand = false, auto_given = false;
    for (const auto *kv : store) {
        auto_given = auto_given || kv->first == "location.auto";
        const bool location = std::find(kLocationKeys.begin(), kLocationKeys.end(), kv->first) != kLocationKeys.end();
        by_hand = by_hand || (location && get_locked(kv->first) != kv->second);
    }
    for (const auto *kv : store) {
        values_[kv->first] = kv->second;
    }
    if (by_hand && !auto_given) {
        values_["location.auto"] = "false";
    }
    save_locked();
    return std::nullopt;
}

void Settings::reset(std::string_view key)
{
    std::lock_guard lock(mu_);
    if (auto it = values_.find(key); it != values_.end()) {
        values_.erase(it);
        save_locked();
    }
}

void Settings::save_locked()
{
    storage_.save(encode_block(values_));
}

} // namespace core
