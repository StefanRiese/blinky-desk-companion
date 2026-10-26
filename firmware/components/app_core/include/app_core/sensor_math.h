// Pure parts of the sensor and RTC drivers (no I²C), so they can be tested on the host:
// SHT4x CRC and conversion, a moving average, PCF85063 BCD time registers.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>

namespace core {

// SHT4x: CRC-8 (polynomial 0x31, init 0xFF) over one 2-byte word.
uint8_t sht4x_crc(uint8_t msb, uint8_t lsb);

struct Climate {
    double temp_c;
    double hum_pct;
};

// sht4x_decode turns the 6 bytes of a measurement (T word, CRC, RH word, CRC) into °C and %
// (humidity clamped to 0…100). nullopt if a CRC is wrong.
std::optional<Climate> sht4x_decode(const std::array<uint8_t, 6> &raw);

// MovingAverage over the last N values.
template <size_t N> class MovingAverage {
public:
    double add(double v)
    {
        sum_ += v - buf_[pos_];
        buf_[pos_] = v;
        pos_ = (pos_ + 1) % N;
        if (count_ < N) {
            count_++;
        }
        // buf_ starts at 0, so until it is full the sum covers only the values added so far.
        return sum_ / static_cast<double>(count_);
    }
    size_t count() const { return count_; }

private:
    std::array<double, N> buf_{};
    double sum_ = 0;
    size_t pos_ = 0, count_ = 0;
};

// PCF85063 time registers 0x04…0x0A (seconds … years), BCD. The "oscillator stopped" bit
// in the seconds register means the time is not valid (battery was empty).
std::optional<std::tm> pcf85063_decode(const std::array<uint8_t, 7> &regs);
std::array<uint8_t, 7> pcf85063_encode(const std::tm &utc);

} // namespace core
