#include "app_core/backlight.h"

namespace core {

bool is_night(const Settings &settings, const std::tm &local)
{
    const int h = local.tm_hour;
    const int from = static_cast<int>(settings.number("ui.night_from"));
    const int to = static_cast<int>(settings.number("ui.night_to"));
    return (from > to && (h >= from || h < to)) || (from < to && h >= from && h < to);
}

BacklightLevel backlight(const Settings &settings, const std::tm *local, uint32_t idle_ms)
{
    const bool night = local != nullptr && is_night(settings, *local);
    const double dim_after = settings.number("ui.dim_after");
    const bool dim = dim_after > 0 && idle_ms >= dim_after * 1000;
    auto level = [&](const char *key) { return static_cast<int>(settings.number(key)); };
    if (night && dim) {
        return {level("ui.dim_night"), "dim_night"};
    }
    if (night) {
        return {level("ui.brightness_night"), "night"};
    }
    if (dim) {
        return {level("ui.dim_day"), "dim_day"};
    }
    return {level("ui.brightness_day"), "day"};
}

} // namespace core
