#include "mood/mood_engine.h"

#include <cstdio>

#include "app_core/backlight.h"
#include "app_core/units.h"

namespace mood {

bool is_rain(int c)
{
    return (c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95;
}

bool is_snow(int c)
{
    return (c >= 71 && c <= 77) || c == 85 || c == 86;
}

void MoodEngine::react(std::string_view mood, std::string_view reason, uint32_t for_ms, uint32_t now)
{
    std::lock_guard lock(mu_);
    reaction_ = {.mood = {std::string(mood), std::string(reason)}, .until = now + for_ms};
}

void MoodEngine::set_override(std::string_view mood, uint32_t ttl_ms, uint32_t now)
{
    std::lock_guard lock(mu_);
    override_ = {.mood = {std::string(mood), "host"}, .until = now + ttl_ms};
}

Mood MoodEngine::evaluate(const Context &ctx, uint32_t now) const
{
    {
        std::lock_guard lock(mu_);
        if (reaction_.active(now)) {
            return reaction_.mood;
        }
        if (ctx.alarm_ringing) {
            return {"surprised", "alarm"};
        }
        if (ctx.countdown_ringing) {
            return {"surprised", "countdown done"};
        }
        if (override_.active(now)) {
            return override_.mood;
        }
    }
    // Notifications follow with the notification overlay (M7).

    char buf[48];
    if (const auto t = store_.number("indoor.temp")) {
        const core::Units u{settings_.get("units.temp"), "kmh", "mm"};
        if (*t > settings_.number("mood.hot")) {
            return {"hot", "indoor " + u.temperature(*t)};
        }
        if (*t < settings_.number("mood.cold")) {
            return {"cold", "indoor " + u.temperature(*t)};
        }
    }
    if (const auto h = store_.number("indoor.hum"); h && *h < settings_.number("mood.dry")) {
        std::snprintf(buf, sizeof buf, "humidity %.0f %%", *h);
        return {"dry", buf};
    }
    if (const auto noise = store_.number("indoor.noise");
        noise && settings_.flag("sound.enabled") && *noise >= settings_.number("sound.too_loud")) {
        std::snprintf(buf, sizeof buf, "too loud (%.0f dB)", *noise);
        return {"angry", buf};
    }
    const auto code = store_.number("weather.now.code");
    if (code && is_rain(static_cast<int>(*code))) {
        return {"sad", "rain outside"};
    }
    if (code && is_snow(static_cast<int>(*code))) {
        return {"cold", "snow outside"};
    }
    if (ctx.local != nullptr && core::is_night(settings_, *ctx.local)) {
        return {"sleepy", "night"};
    }
    if (ctx.host_lost) {
        return {"sad", "host link lost"};
    }
    if (code && *code <= 1 && store_.number("weather.now.is_day").value_or(1) != 0) {
        return {"happy", "sunny"};
    }
    return {"neutral", "idle"};
}

} // namespace mood
