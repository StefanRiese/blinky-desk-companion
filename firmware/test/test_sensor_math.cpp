#include <doctest/doctest.h>

#include "app_core/sensor_math.h"

TEST_CASE("SHT4x CRC matches the datasheet example")
{
    CHECK(core::sht4x_crc(0xBE, 0xEF) == 0x92); // Sensirion's documented test vector
}

TEST_CASE("SHT4x decode converts and checks CRCs")
{
    // raw 0x6666 → -45 + 175 * 0.4 = 25 °C; raw 0x8000 → -6 + 125 * 0.50001 ≈ 56.5 %
    std::array<uint8_t, 6> raw = {0x66, 0x66, core::sht4x_crc(0x66, 0x66), 0x80, 0x00, core::sht4x_crc(0x80, 0x00)};
    const auto c = core::sht4x_decode(raw);
    REQUIRE(c.has_value());
    CHECK(c->temp_c == doctest::Approx(25.0).epsilon(0.001));
    CHECK(c->hum_pct == doctest::Approx(56.5).epsilon(0.001));

    raw[2] ^= 1; // broken CRC
    CHECK_FALSE(core::sht4x_decode(raw).has_value());

    std::array<uint8_t, 6> dry = {0x66, 0x66, core::sht4x_crc(0x66, 0x66), 0x00, 0x00, core::sht4x_crc(0x00, 0x00)};
    CHECK(core::sht4x_decode(dry)->hum_pct == 0.0); // clamped (raw 0 would be -6 %)
}

TEST_CASE("MovingAverage averages the last N values")
{
    core::MovingAverage<3> avg;
    CHECK(avg.add(10) == 10);
    CHECK(avg.add(20) == 15);
    CHECK(avg.add(30) == 20);
    CHECK(avg.add(40) == 30); // 20, 30, 40
    CHECK(avg.count() == 3);
}

TEST_CASE("PCF85063 BCD time round-trips, stopped oscillator means no time")
{
    std::tm t{};
    t.tm_year = 126; // 2026
    t.tm_mon = 9;    // October
    t.tm_mday = 26;
    t.tm_hour = 23;
    t.tm_min = 59;
    t.tm_sec = 58;
    t.tm_wday = 1;
    const auto regs = core::pcf85063_encode(t);
    CHECK(regs[0] == 0x58);
    CHECK(regs[2] == 0x23);
    CHECK(regs[5] == 0x10);
    CHECK(regs[6] == 0x26);
    const auto back = core::pcf85063_decode(regs);
    REQUIRE(back.has_value());
    CHECK(back->tm_year == 126);
    CHECK(back->tm_mon == 9);
    CHECK(back->tm_mday == 26);
    CHECK(back->tm_hour == 23);
    CHECK(back->tm_sec == 58);

    auto stopped = regs;
    stopped[0] |= 0x80;
    CHECK_FALSE(core::pcf85063_decode(stopped).has_value());
    auto nonsense = regs;
    nonsense[5] = 0x13; // month 13
    CHECK_FALSE(core::pcf85063_decode(nonsense).has_value());
}
