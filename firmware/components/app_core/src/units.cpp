#include "app_core/units.h"

#include <cmath>
#include <cstdio>

namespace core {
namespace {

std::string format(const char *fmt, double v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, fmt, v);
    return buf;
}

} // namespace

std::string Units::temperature(double celsius, int decimals, bool unit) const
{
    const bool f = temp == "F";
    const double v = f ? celsius * 9 / 5 + 32 : celsius;
    std::string s = format(decimals > 0 ? "%.1f" : "%.0f", decimals > 0 ? v : std::round(v));
    if (s == "-0") {
        s = "0";
    }
    s += "°"; // °
    if (unit) {
        s += f ? "F" : "C";
    }
    return s;
}

std::string Units::wind_speed(double kmh) const
{
    if (wind == "mph") {
        return format("%.0f mph", std::round(kmh / 1.609));
    }
    if (wind == "ms") {
        return format("%.1f m/s", kmh / 3.6);
    }
    return format("%.0f km/h", std::round(kmh));
}

std::string Units::rain_amount(double mm) const
{
    if (rain == "in") {
        return format("%.2f in", mm / 25.4);
    }
    return format("%.1f mm", mm);
}

} // namespace core
