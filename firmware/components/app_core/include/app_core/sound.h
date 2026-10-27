// Sound, without hardware: what the microphone task makes of the sound level, and the beeps
// for a ringing alarm or a finished countdown.
//
// SoundLevel follows the emulator (host/internal/sim/sound.go): it gets the level in dB ten
// times a second. Only levels are used; no audio is kept or sent.
//   - a sudden loud noise (≥ sound.loud and 20 dB above the room) → Loud (startled eyes)
//   - voices (10 dB above the room for 0.8 s, short pauses allowed) → Voices (curious eyes)
//   - the 30 s average is indoor.noise; above sound.too_loud the eyes look angry
// The room level (baseline) falls quickly and rises slowly, so lasting noise becomes the
// new normal after about a minute while claps and voices hardly move it.
#pragma once

#include <cstdint>
#include <span>

namespace core {

class SoundLevel {
public:
    static constexpr uint32_t kTickMs = 100;
    enum class Event { None, Loud, Voices };

    // add takes one level (dB); loud is the setting sound.loud.
    Event add(double db, uint32_t now, double loud);
    double average() const { return avg_; } // 30 s average
    double base() const { return base_; }   // the room's level

private:
    bool started_ = false;
    double avg_ = 0, base_ = 0;
    bool voice_ = false;
    uint32_t voice_since_ = 0, voice_last_ = 0, last_react_ = 0;
    bool reacted_ = false;
};

// level_db turns 32-bit microphone samples into a level in dB, roughly dB SPL: RMS in dB
// below full scale plus spl_at_full_scale (≈ 120 for a −26 dBFS / 94 dB SPL mic).
double level_db(std::span<const int32_t> samples, double spl_at_full_scale = 120.0);

// Tunes repeat until stopped.
enum class Tune { None, Alarm, Countdown };

// tone_hz returns the frequency at t ms into a tune (0 = a pause).
int tone_hz(Tune tune, uint32_t t_ms);

// synth fills out with the tune from sample index first on (32-bit samples at rate Hz,
// volume 0 … 1), with short fades so the beeps don't click.
void synth(Tune tune, uint64_t first, int rate, double volume, std::span<int32_t> out);

} // namespace core
