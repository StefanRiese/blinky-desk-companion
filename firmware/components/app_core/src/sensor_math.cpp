#include "app_core/sensor_math.h"

#include <algorithm>

namespace core {

uint8_t sht4x_crc(uint8_t msb, uint8_t lsb)
{
    uint8_t crc = 0xFF;
    for (uint8_t b : {msb, lsb}) {
        crc ^= b;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

std::optional<Climate> sht4x_decode(const std::array<uint8_t, 6> &raw)
{
    if (sht4x_crc(raw[0], raw[1]) != raw[2] || sht4x_crc(raw[3], raw[4]) != raw[5]) {
        return std::nullopt;
    }
    const double t = (raw[0] << 8) | raw[1];
    const double rh = (raw[3] << 8) | raw[4];
    return Climate{-45.0 + 175.0 * t / 65535.0, std::clamp(-6.0 + 125.0 * rh / 65535.0, 0.0, 100.0)};
}

namespace {
uint8_t from_bcd(uint8_t v)
{
    return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F));
}
uint8_t to_bcd(int v)
{
    return static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
}
} // namespace

std::optional<std::tm> pcf85063_decode(const std::array<uint8_t, 7> &r)
{
    if (r[0] & 0x80) {
        return std::nullopt; // oscillator stopped: time lost
    }
    std::tm t{};
    t.tm_sec = from_bcd(r[0] & 0x7F);
    t.tm_min = from_bcd(r[1] & 0x7F);
    t.tm_hour = from_bcd(r[2] & 0x3F); // 24-hour mode
    t.tm_mday = from_bcd(r[3] & 0x3F);
    t.tm_wday = r[4] & 0x07;
    t.tm_mon = from_bcd(r[5] & 0x1F) - 1;
    t.tm_year = 100 + from_bcd(r[6]); // 2000…2099
    if (t.tm_sec > 59 || t.tm_min > 59 || t.tm_hour > 23 || t.tm_mday < 1 || t.tm_mday > 31 || t.tm_mon < 0 ||
        t.tm_mon > 11) {
        return std::nullopt;
    }
    return t;
}

std::array<uint8_t, 7> pcf85063_encode(const std::tm &t)
{
    return {to_bcd(t.tm_sec), to_bcd(t.tm_min), to_bcd(t.tm_hour), to_bcd(t.tm_mday),
            static_cast<uint8_t>(t.tm_wday & 0x07), to_bcd(t.tm_mon + 1), to_bcd(t.tm_year % 100)};
}

} // namespace core
