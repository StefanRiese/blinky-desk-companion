#include <doctest/doctest.h>

#include "app_core/alarm_clock.h"
#include "app_core/countdown.h"

using core::Action;

TEST_CASE("countdown: set by swiping, wraps, start/pause/reset, rings at zero")
{
    core::Countdown c;
    using E = core::Countdown::Event;
    E ev;
    CHECK_FALSE(c.input(Action::Shake, -1, 0, &ev)); // nothing set: a normal shake

    c.input(Action::SwipeUp, 0.25f, 0);  // minutes
    c.input(Action::SwipeUp, 0.25f, 0);
    c.input(Action::SwipeDown, 0.75f, 0); // seconds wrap 0 → 59
    CHECK(c.view(0).left_ms == 2 * 60000 + 59000);
    CHECK(c.view(0).set_ms == c.view(0).left_ms);
    core::Countdown wrap;
    wrap.input(Action::SwipeDown, 0.25f, 0); // minutes wrap 0 → 99
    CHECK(wrap.view(0).left_ms == 99 * 60000);

    core::Countdown t;
    t.input(Action::SwipeUp, 0.25f, 0); // 1:00
    t.input(Action::Shake, -1, 1000, &ev);
    CHECK(ev == E::Started);
    CHECK(t.view(1000).running);
    t.input(Action::SwipeUp, 0.25f, 2000); // ignored while running
    CHECK(t.view(2000).set_ms == 60000);
    t.input(Action::Shake, -1, 31000, &ev);
    CHECK(ev == E::Paused);
    CHECK(t.view(31000).left_ms == 30000);
    CHECK(t.view(31000).paused());
    t.input(Action::LongPress, -1, 32000, &ev);
    CHECK(ev == E::Reset);
    CHECK(t.view(32000).left_ms == 60000);

    t.input(Action::Shake, -1, 40000); // runs to 100 s
    CHECK(t.tick(99000) == E::None);
    CHECK(t.tick(100000) == E::Done);
    CHECK(t.ringing());
    t.input(Action::Shake, -1, 101000, &ev);
    CHECK(ev == E::Stopped);
    CHECK(t.view(101000).left_ms == 60000); // back to the set time

    t.input(Action::Shake, -1, 0);
    t.tick(60000);
    CHECK(t.ringing());
    CHECK(t.tick(60000 + core::Countdown::kRingMs) == E::Stopped); // stops by itself
}

namespace {
std::tm at(int wday, int h, int m)
{
    std::tm t{};
    t.tm_year = 126;
    t.tm_mon = 9;
    t.tm_mday = 26 + (wday - 1); // 26 Oct 2026 is a Monday
    t.tm_wday = wday;
    t.tm_hour = h;
    t.tm_min = m;
    return t;
}
} // namespace

TEST_CASE("alarm list JSON round-trips; invalid lists are rejected")
{
    const auto list = core::parse_alarms(
        R"([{"at":"07:00","days":["mon","tue","wed","thu","fri"],"on":true},{"at":"09:30","on":false}])");
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);
    CHECK((*list)[0].on_day(1));
    CHECK_FALSE((*list)[0].on_day(0));
    CHECK((*list)[1].days == 0x7F); // no days = every day
    CHECK(core::alarms_json(*list) ==
          R"([{"at":"07:00","days":["mon","tue","wed","thu","fri"],"on":true},{"at":"09:30","on":false}])");
    CHECK_FALSE(core::parse_alarms(R"([{"at":"25:00"}])").has_value());
    CHECK_FALSE(core::parse_alarms("not json").has_value());
}

TEST_CASE("alarm clock: edit on the page, ring, snooze, stop")
{
    core::MemoryStorage storage;
    core::Settings settings(storage);
    core::AlarmClock clock(settings);
    using E = core::AlarmClock::Event;
    const std::tm mon0700 = at(1, 7, 0);

    // Factory alarms: 07:00 Mon–Fri off, 09:00 Sat–Sun off.
    CHECK(clock.next(mon0700) == "");
    CHECK(clock.input(Action::Tap, -1, true, mon0700, 0)); // switch the first one on
    CHECK(clock.alarms()[0].on);
    CHECK(clock.next(at(1, 6, 0)) == "Mon 07:00");
    CHECK(clock.next(mon0700) == "Tue 07:00");      // not the current minute again
    CHECK(clock.next(at(5, 8, 0)) == "Mon 07:00");  // Friday after 7 → Monday
    CHECK_FALSE(clock.input(Action::Tap, -1, false, mon0700, 0)); // not on the page: not ours

    clock.input(Action::SwipeUp, 0.25f, true, mon0700, 0);  // hour 08
    clock.input(Action::SwipeDown, 0.75f, true, mon0700, 0); // minute 55 (5-minute steps)
    CHECK(clock.alarms()[0].hour == 8);
    CHECK(clock.alarms()[0].minute == 55);
    clock.input(Action::LongPress, -1, true, mon0700, 0);
    CHECK(clock.selected() == 1);

    // Ring at 08:55 on a weekday, once per minute.
    CHECK(clock.tick(nullptr, 0) == E::None); // clock not set
    const std::tm t = at(2, 8, 55);
    CHECK(clock.tick(&t, 1000) == E::Ringing);
    CHECK(clock.ringing());
    CHECK(clock.ring_at() == "08:55");
    CHECK(clock.selected() == 0);
    E ev;
    CHECK(clock.input(Action::Tap, -1, false, t, 2000, &ev)); // snooze, from any page
    CHECK(ev == E::Snoozed);
    CHECK(clock.snoozed_until() == "09:04");
    CHECK(clock.tick(&t, 3000) == E::None); // same minute: does not ring again
    CHECK(clock.tick(&t, 2000 + 9 * 60000) == E::Ringing);
    clock.input(Action::Shake, -1, false, t, 2000 + 9 * 60000 + 1, &ev);
    CHECK(ev == E::Stopped);
    CHECK(clock.snoozed_until().empty());

    const std::tm wed = at(3, 8, 55);
    clock.tick(&wed, 1'000'000);
    CHECK(clock.tick(&wed, 1'000'000 + core::AlarmClock::kRingMs) == E::Stopped); // stops by itself
    const std::tm sat = at(6, 8, 55);
    CHECK(clock.tick(&sat, 2'000'000) == E::None); // not on weekends
}
