#include "app_core/sound.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace core {
namespace {

constexpr double kTick = SoundLevel::kTickMs;
constexpr double kVoiceAbove = 10;   // dB above the room counts as voices
constexpr uint32_t kVoiceForMs = 800;
constexpr uint32_t kVoiceGapMs = 400; // pauses between syllables don't end a stretch
constexpr double kLoudAbove = 20;     // dB above the room for a startle
constexpr uint32_t kCooldownMs = 3000;
constexpr uint32_t kFadeMs = 5;

// One beep of a tune: from, to (ms within the cycle), frequency.
struct Beep {
    uint32_t from, to;
    int hz;
};
// Alarm: beep-beep, pause (1 s); countdown: three short high beeps, pause (1.3 s).
constexpr Beep kAlarm[] = {{0, 150, 880}, {250, 400, 880}};
constexpr uint32_t kAlarmCycle = 1000;
constexpr Beep kCountdown[] = {{0, 100, 1319}, {200, 300, 1319}, {400, 500, 1319}};
constexpr uint32_t kCountdownCycle = 1300;

std::span<const Beep> beeps(Tune t, uint32_t &cycle)
{
    switch (t) {
    case Tune::Alarm:
        cycle = kAlarmCycle;
        return kAlarm;
    case Tune::Countdown:
        cycle = kCountdownCycle;
        return kCountdown;
    case Tune::None:
        break;
    }
    cycle = 1;
    return {};
}

} // namespace

SoundLevel::Event SoundLevel::add(double db, uint32_t now, double loud)
{
    if (!started_) {
        started_ = true;
        avg_ = base_ = db;
    }
    avg_ += (db - avg_) * (kTick / 30000.0);
    base_ += (db - base_) * (kTick / (db > base_ ? 60000.0 : 20000.0));

    const bool cooled = !reacted_ || now - last_react_ >= kCooldownMs;
    if (db >= loud && db - base_ >= kLoudAbove) {
        voice_ = false;
        if (!cooled) {
            return Event::None;
        }
        reacted_ = true;
        last_react_ = now;
        return Event::Loud;
    }
    if (db >= base_ + kVoiceAbove) {
        if (!voice_ || now - voice_last_ > kVoiceGapMs) {
            voice_ = true;
            voice_since_ = now;
        }
        voice_last_ = now;
        if (now - voice_since_ >= kVoiceForMs && cooled) {
            reacted_ = true;
            last_react_ = now;
            return Event::Voices;
        }
    }
    return Event::None;
}

double level_db(std::span<const int16_t> samples, double spl_at_full_scale)
{
    if (samples.empty()) {
        return 0;
    }
    double mean = 0;
    for (int16_t s : samples) {
        mean += s;
    }
    mean /= static_cast<double>(samples.size()); // the mic has a DC offset
    double sum = 0;
    for (int16_t s : samples) {
        const double v = (s - mean) / 32768.0;
        sum += v * v;
    }
    const double rms = std::sqrt(sum / static_cast<double>(samples.size()));
    return std::max(0.0, 20 * std::log10(std::max(rms, 1e-9)) + spl_at_full_scale);
}

int tone_hz(Tune tune, uint32_t t_ms)
{
    uint32_t cycle = 1;
    const auto list = beeps(tune, cycle);
    const uint32_t t = t_ms % cycle;
    for (const Beep &b : list) {
        if (t >= b.from && t < b.to) {
            return b.hz;
        }
    }
    return 0;
}

void synth(Tune tune, uint64_t first, int rate, double volume, std::span<int16_t> out)
{
    uint32_t cycle = 1;
    const auto list = beeps(tune, cycle);
    const double amp = std::clamp(volume, 0.0, 1.0) * 32767.0;
    for (size_t i = 0; i < out.size(); i++) {
        const uint64_t n = first + i;
        const double ms = static_cast<double>(n) * 1000.0 / rate;
        const double t = std::fmod(ms, static_cast<double>(cycle));
        double v = 0;
        for (const Beep &b : list) {
            if (t >= b.from && t < b.to) {
                const double fade = std::min({1.0, (t - b.from) / kFadeMs, (b.to - t) / kFadeMs});
                v = fade * std::sin(2 * std::numbers::pi * b.hz * static_cast<double>(n) / rate);
                break;
            }
        }
        out[i] = static_cast<int16_t>(v * amp);
    }
}

} // namespace core
