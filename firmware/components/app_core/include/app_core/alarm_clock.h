// Alarm clock, same behaviour as the emulator (host/internal/sim/alarm.go). Alarms live in
// the settings (alarm.list, JSON), so they ring without WiFi or host once the clock is set.
// On the page: swipe up/down on the hour (left) or minute (right, 5-minute steps), tap =
// on/off, long press = next alarm. While ringing: shake = stop, tap = snooze
// (alarm.snooze_min); it stops by itself after 5 minutes.
#pragma once

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app_core/countdown.h" // Action
#include "app_core/settings.h"

namespace core {

struct Alarm {
    int hour = 7, minute = 0;
    uint8_t days = 0x7F; // bit 0 = Sunday … bit 6 = Saturday (struct tm order); 0x7F = every day
    bool on = false;
    bool on_day(int wday) const { return (days >> wday) & 1; }
};

constexpr size_t kMaxAlarms = 4;

// parse_alarms reads alarm.list ([{"at":"07:00","days":["mon",…],"on":true}, …]; no days =
// every day). nullopt if the JSON is invalid.
std::optional<std::vector<Alarm>> parse_alarms(std::string_view json);
std::string alarms_json(const std::vector<Alarm> &alarms);

class AlarmClock {
public:
    enum class Event { None, Ringing, Snoozed, Stopped };
    static constexpr uint32_t kRingMs = 5 * 60'000;
    static constexpr int kMinuteStep = 5;

    explicit AlarmClock(Settings &settings) : settings_(settings) {}

    std::vector<Alarm> alarms() const;
    int selected() const { return sel_; }
    bool ringing() const { return ringing_; }
    const std::string &ring_at() const { return ring_at_; } // "07:00" while ringing
    // snoozed_until is the local time it rings again ("07:09"), empty if not snoozed.
    const std::string &snoozed_until() const { return snooze_text_; }

    // input handles an action. While ringing it takes every action (on any page); otherwise
    // only on the alarm page (on_page). Returns whether the action was used.
    bool input(Action a, float x, bool on_page, const std::tm &local, uint32_t now_ms, Event *ev = nullptr);

    // tick runs once a second with the local time (nullptr while the clock is not set).
    Event tick(const std::tm *local, uint32_t now_ms);

    // next returns the next alarm after local, like "Mon 07:00", or "" if all are off.
    std::string next(const std::tm &local) const;

private:
    void ring(int index, uint32_t now_ms);

    Settings &settings_;
    int sel_ = 0;
    bool ringing_ = false;
    uint32_t ring_start_ = 0;
    int ring_index_ = 0;
    std::string ring_at_;
    bool snoozed_ = false;
    uint32_t snooze_until_ = 0;
    std::string snooze_text_;
    std::string last_fired_; // "YYYY-MM-DD HH:MM #i": an alarm fires once per minute
};

} // namespace core
