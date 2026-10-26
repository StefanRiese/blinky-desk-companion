// Demo data for the simulator and screenshots: a cloudy autumn morning.
#pragma once

#include <ctime>

#include "app_core/data_store.h"

namespace sim {

inline void fill_demo(core::DataStore &store)
{
    using core::Origin;
    store.put("weather.now.temp", 8.4, Origin::Host, 0);
    store.put("weather.now.code", 3.0, Origin::Host, 0);
    store.put("weather.now.is_day", 1.0, Origin::Host, 0);
    store.put("weather.now.hum", 82.0, Origin::Host, 0);
    store.put("weather.now.wind", 14.0, Origin::Host, 0);
    store.put("weather.rain_next_2h", 0.4, Origin::Host, 0);
    store.put("indoor.temp", 21.6, Origin::Local, 0);
    store.put("indoor.hum", 44.0, Origin::Local, 0);
}

// fixed_time is Monday, 26 October 2026, 07:16 (for reproducible screenshots).
inline bool fixed_time(std::tm &t)
{
    t = {};
    t.tm_year = 126;
    t.tm_mon = 9;
    t.tm_mday = 26;
    t.tm_wday = 1;
    t.tm_hour = 7;
    t.tm_min = 16;
    return true;
}

} // namespace sim
