#include "app_core/alarm_clock.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace core {
namespace {

constexpr std::array<const char *, 7> kDayNames = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
constexpr std::array<const char *, 7> kDayShort = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

std::string hhmm(int h, int m)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", h, m);
    return buf;
}

} // namespace

std::optional<std::vector<Alarm>> parse_alarms(std::string_view json)
{
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size()) || !doc.is<JsonArrayConst>()) {
        return std::nullopt;
    }
    std::vector<Alarm> out;
    for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
        if (out.size() == kMaxAlarms) {
            break;
        }
        Alarm a;
        int h = 0, m = 0;
        const char *at = o["at"] | "";
        if (std::sscanf(at, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
            return std::nullopt;
        }
        a.hour = h;
        a.minute = m;
        a.on = o["on"] | false;
        if (JsonArrayConst days = o["days"]; !days.isNull() && days.size() > 0) {
            a.days = 0;
            for (const char *d : days) {
                for (int i = 0; i < 7; i++) {
                    if (d != nullptr && std::string_view(d) == kDayNames[i]) {
                        a.days |= 1 << i;
                    }
                }
            }
        }
        out.push_back(a);
    }
    return out;
}

std::string alarms_json(const std::vector<Alarm> &alarms)
{
    JsonDocument doc;
    for (const Alarm &a : alarms) {
        JsonObject o = doc.add<JsonObject>();
        o["at"] = hhmm(a.hour, a.minute);
        if (a.days != 0x7F) {
            JsonArray days = o["days"].to<JsonArray>();
            for (int i = 1; i <= 7; i++) { // Monday first, as the setup page writes them
                if (a.on_day(i % 7)) {
                    days.add(kDayNames[i % 7]);
                }
            }
        }
        o["on"] = a.on;
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

std::vector<Alarm> AlarmClock::alarms() const
{
    return parse_alarms(settings_.get("alarm.list")).value_or(std::vector<Alarm>{});
}

void AlarmClock::ring(int index, uint32_t now)
{
    const auto list = alarms();
    ringing_ = true;
    ring_start_ = now;
    ring_index_ = index;
    sel_ = index;
    ring_at_ = index < static_cast<int>(list.size()) ? hhmm(list[index].hour, list[index].minute) : "";
    snoozed_ = false;
    snooze_text_.clear();
}

bool AlarmClock::input(Action a, float x, bool on_page, const std::tm &local, uint32_t now, Event *ev)
{
    Event e = Event::None;
    if (ringing_) {
        if (a == Action::Shake) {
            ringing_ = false;
            e = Event::Stopped;
        } else if (a == Action::Tap) {
            ringing_ = false;
            snoozed_ = true;
            const int minutes = static_cast<int>(settings_.number("alarm.snooze_min"));
            snooze_until_ = now + static_cast<uint32_t>(minutes) * 60'000;
            const int at = local.tm_hour * 60 + local.tm_min + minutes;
            snooze_text_ = hhmm(at / 60 % 24, at % 60);
            e = Event::Snoozed;
        }
        if (ev != nullptr) {
            *ev = e;
        }
        return true; // nothing else while ringing
    }
    auto list = alarms();
    if (!on_page || list.empty()) {
        return false;
    }
    sel_ = std::min(sel_, static_cast<int>(list.size()) - 1);
    Alarm &al = list[sel_];
    switch (a) {
    case Action::SwipeUp:
    case Action::SwipeDown: {
        const int step = a == Action::SwipeUp ? 1 : -1;
        if (x >= 0.5f) {
            al.minute = ((al.minute / kMinuteStep + step) * kMinuteStep + 60) % 60;
        } else {
            al.hour = (al.hour + step + 24) % 24;
        }
        break;
    }
    case Action::Tap:
        al.on = !al.on;
        break;
    case Action::LongPress:
        sel_ = (sel_ + 1) % static_cast<int>(list.size());
        return true;
    default:
        return false;
    }
    settings_.set("alarm.list", alarms_json(list));
    return true;
}

AlarmClock::Event AlarmClock::tick(const std::tm *local, uint32_t now)
{
    if (ringing_) {
        if (static_cast<int32_t>(now - ring_start_) >= static_cast<int32_t>(kRingMs)) {
            ringing_ = false;
            return Event::Stopped;
        }
        return Event::None;
    }
    if (snoozed_ && static_cast<int32_t>(now - snooze_until_) >= 0) {
        ring(ring_index_, now);
        return Event::Ringing;
    }
    if (local == nullptr) {
        return Event::None; // the clock is not set
    }
    const auto list = alarms();
    for (int i = 0; i < static_cast<int>(list.size()); i++) {
        const Alarm &a = list[i];
        char key[96]; // room for any int, so -Wformat-truncation stays quiet
        std::snprintf(key, sizeof key, "%04d-%02d-%02d %02d:%02d #%d", local->tm_year + 1900, local->tm_mon + 1,
                      local->tm_mday, local->tm_hour, local->tm_min, i);
        if (a.on && local->tm_hour == a.hour && local->tm_min == a.minute && a.on_day(local->tm_wday) &&
            last_fired_ != key) {
            last_fired_ = key;
            ring(i, now);
            return Event::Ringing;
        }
    }
    return Event::None;
}

std::string AlarmClock::next(const std::tm &local) const
{
    const int now_min = local.tm_hour * 60 + local.tm_min;
    int best = -1, best_day = 0, best_min = 0;
    for (const Alarm &a : alarms()) {
        if (!a.on) {
            continue;
        }
        const int at = a.hour * 60 + a.minute;
        for (int d = 0; d <= 7; d++) {
            if ((d > 0 || at > now_min) && a.on_day((local.tm_wday + d) % 7)) {
                const int when = d * 1440 + at;
                if (best < 0 || when < best) {
                    best = when;
                    best_day = (local.tm_wday + d) % 7;
                    best_min = at;
                }
                break;
            }
        }
    }
    if (best < 0) {
        return "";
    }
    return std::string(kDayShort[best_day]) + " " + hhmm(best_min / 60, best_min % 60);
}

} // namespace core
