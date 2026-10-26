// Icon symbols (Material Icons, Apache-2.0) as UTF-8 strings for the icon fonts
// (ui::font::icons). Keep in sync with the ICONS list in tools/fonts.sh.
#pragma once

namespace ui::icon {

// Weather
inline constexpr const char *kSun = "\xEE\x90\xB0";          // U+E430 wb_sunny
inline constexpr const char *kPartlyCloudy = "\xEE\x90\xAD"; // U+E42D wb_cloudy
inline constexpr const char *kCloud = "\xEE\x8A\xBD";        // U+E2BD cloud
inline constexpr const char *kFog = "\xEE\xA0\x98";          // U+E818 foggy
inline constexpr const char *kRain = "\xEF\x86\xAD";         // U+F1AD umbrella
inline constexpr const char *kSnow = "\xEE\xAC\xBB";         // U+EB3B ac_unit
inline constexpr const char *kThunder = "\xEE\xAF\x9B";      // U+EBDB thunderstorm
inline constexpr const char *kMoon = "\xEE\xA9\x86";         // U+EA46 nights_stay
inline constexpr const char *kThermometer = "\xEF\x81\xB6";  // U+F076 thermostat
inline constexpr const char *kHumidity = "\xEE\x9E\x98";     // U+E798 water_drop
inline constexpr const char *kWind = "\xEE\xBF\x98";         // U+EFD8 air

// Links and status
inline constexpr const char *kWifi = "\xEE\x98\xBE";            // U+E63E wifi
inline constexpr const char *kWifiOff = "\xEE\x99\x88";         // U+E648 wifi_off
inline constexpr const char *kUsb = "\xEE\x87\xA0";             // U+E1E0 usb
inline constexpr const char *kNetwork = "\xEE\xAC\xAF";         // U+EB2F lan (TCP host)
inline constexpr const char *kLinkOff = "\xEE\x85\xAF";         // U+E16F link_off
inline constexpr const char *kBattery = "\xEE\x86\xA4";         // U+E1A4 battery_full
inline constexpr const char *kBatteryCharging = "\xEE\x86\xA3"; // U+E1A3 battery_charging_full
inline constexpr const char *kBatteryLow = "\xEE\x86\x9C";      // U+E19C battery_alert

// Features and notifications
inline constexpr const char *kAlarm = "\xEE\xA1\x95";   // U+E855 alarm
inline constexpr const char *kTimer = "\xEE\x90\xA5";   // U+E425 timer
inline constexpr const char *kBell = "\xEE\x9F\xB4";    // U+E7F4 notifications
inline constexpr const char *kWarning = "\xEE\x80\x82"; // U+E002 warning
inline constexpr const char *kOk = "\xEE\xA1\xAC";      // U+E86C check_circle
inline constexpr const char *kError = "\xEE\x80\x80";   // U+E000 error
inline constexpr const char *kHome = "\xEE\xA2\x8A";    // U+E88A home
inline constexpr const char *kHeart = "\xEE\xA1\xBD";   // U+E87D favorite
inline constexpr const char *kSound = "\xEE\x81\x90";   // U+E050 volume_up

} // namespace ui::icon
