// MoodEngine: which mood the eyes show and why, as in the emulator (moodLocked in
// host/internal/sim/device.go). First match wins:
//   reaction (boop, shaken, loud noise, voices) > ringing alarm > finished countdown >
//   host override ("mood" message) > indoor hot / cold > dry air > too loud for a while >
//   rain / snow outside > night > host link lost > sunny > neutral
// Thresholds come from the settings (mood.hot, mood.cold, mood.dry, sound.too_loud, night
// hours); data from the DataStore (indoor.*, weather.now.*, indoor.noise), fresh values only.
// react() and set_override() are thread-safe (the sound task reacts too).
#pragma once

#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

#include "app_core/data_store.h"
#include "app_core/settings.h"

namespace mood {

// What the engine can't read from the settings and the store.
struct Context {
    bool alarm_ringing = false;
    bool countdown_ringing = false;
    const std::tm *local = nullptr; // local time; null while the clock is not set
    bool host_lost = false;         // the TCP host was connected and is gone
};

struct Mood {
    std::string name, reason;
    bool operator==(const Mood &) const = default;
};

class MoodEngine {
public:
    MoodEngine(const core::Settings &settings, const core::DataStore &store) : settings_(settings), store_(store) {}

    // react shows a mood for a moment, before everything else (touch, shake, sounds).
    void react(std::string_view mood, std::string_view reason, uint32_t for_ms, uint32_t now);
    // set_override is the host's "mood" message: shown for ttl_ms unless something more
    // urgent happens (a reaction, an alarm).
    void set_override(std::string_view mood, uint32_t ttl_ms, uint32_t now);

    Mood evaluate(const Context &ctx, uint32_t now) const;

private:
    struct Timed {
        Mood mood;
        uint32_t until = 0;
        bool active(uint32_t now) const { return !mood.name.empty() && static_cast<int32_t>(until - now) > 0; }
    };

    const core::Settings &settings_;
    const core::DataStore &store_;
    mutable std::mutex mu_;
    Timed reaction_, override_;
};

// is_rain / is_snow classify WMO weather codes (as used by Open-Meteo).
bool is_rain(int code);
bool is_snow(int code);

} // namespace mood
