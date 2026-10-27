// Night hours and the backlight level, as in the emulator (host/internal/sim/backlight.go):
// ui.brightness_day / ui.brightness_night (night = ui.night_from … ui.night_to), and after
// ui.dim_after seconds without activity ui.dim_day / ui.dim_night. Touch, notifications and
// ringing alarms count as activity. ui.dim_after = 0 never dims.
#pragma once

#include <cstdint>
#include <ctime>
#include <string_view>

#include "app_core/settings.h"

namespace core {

// is_night reports whether the local hour lies in the night hours.
bool is_night(const Settings &settings, const std::tm &local);

struct BacklightLevel {
    int level;             // 0 … 255
    std::string_view mode; // day | night | dim_day | dim_night
    bool operator==(const BacklightLevel &) const = default;
};

// backlight returns the level for the time of day (local is null while the clock is not
// set: day) and the time since the last activity.
BacklightLevel backlight(const Settings &settings, const std::tm *local, uint32_t idle_ms);

} // namespace core
