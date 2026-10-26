// Display units (units.* settings). Data and thresholds stay metric (°C, km/h, mm); only
// what the display shows is converted. Same rules as the emulator.
#pragma once

#include <string>

namespace core {

struct Units {
    std::string temp = "C";   // C | F
    std::string wind = "kmh"; // kmh | mph | ms
    std::string rain = "mm";  // mm | in

    // temperature formats °C in the display unit: "21.5°C" (decimals 0 or 1).
    std::string temperature(double celsius, int decimals = 1, bool unit = true) const;
    std::string wind_speed(double kmh) const; // "12 km/h", "7 mph", "3.3 m/s"
    std::string rain_amount(double mm) const; // "1.2 mm", "0.05 in"
};

} // namespace core
