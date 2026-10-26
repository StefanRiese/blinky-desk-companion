#include "app_core/time_zone.h"

#include <array>
#include <utility>

namespace core {
namespace {

using Zone = std::pair<std::string_view, std::string_view>;

constexpr std::array kZones = {
    Zone{"UTC", "UTC0"},
    Zone{"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    Zone{"Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"},
    Zone{"Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
    Zone{"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    Zone{"Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3"},
    Zone{"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    Zone{"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    Zone{"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    Zone{"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
    Zone{"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
    Zone{"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    Zone{"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    Zone{"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    Zone{"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    Zone{"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    Zone{"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    Zone{"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    Zone{"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    Zone{"Asia/Tokyo", "JST-9"},
    Zone{"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};

} // namespace

std::optional<std::string_view> posix_tz(std::string_view iana)
{
    for (const auto &[name, rule] : kZones) {
        if (name == iana) {
            return rule;
        }
    }
    return std::nullopt;
}

} // namespace core
