#include "ui/eye_model.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ui {
namespace {

struct Preset {
    std::string_view name;
    EyeShape shape;
};

// Keep in sync with PRESETS in host/internal/sim/web/index.html.
constexpr std::array kPresets = {
    Preset{"neutral", {}},
    Preset{"happy", {.w = 1.05f, .bottom = 0.36f, .dy = -6}},
    Preset{"sad", {.h = 0.9f, .top = 0.38f, .tilt = 0.4f, .dy = 10, .color = 0x5AA8FF}},
    Preset{"angry", {.h = 0.95f, .top = 0.45f, .tilt = -0.5f, .color = 0xFF5A5A}},
    Preset{"sleepy", {.w = 1.05f, .h = 0.8f, .top = 0.62f, .tilt = 0.05f, .dy = 14}},
    Preset{"surprised", {.w = 1.12f, .h = 1.22f, .dy = -8}},
    Preset{"curious", {.top = 0.12f, .asym = 0.25f}},
    Preset{"hot", {.h = 0.9f, .top = 0.25f, .tilt = 0.2f, .color = 0xFF9640}},
    Preset{"cold", {.w = 0.95f, .h = 0.82f, .top = 0.1f, .bottom = 0.12f, .color = 0x9AC4FF}},
    Preset{"dry", {.h = 0.72f, .top = 0.3f, .tilt = 0.12f, .color = 0xE8C26A}},
    Preset{"dizzy", {.color = 0xC38BFF}},
    Preset{"love", {.bottom = 0.3f, .color = 0xFF6FA8}},
    Preset{"focused", {.h = 0.7f, .top = 0.2f}},
    Preset{"tired", {.h = 0.85f, .top = 0.45f, .tilt = 0.2f, .dy = 10}},
    Preset{"skeptical", {.top = 0.25f, .asym = -0.3f}},
    Preset{"scared", {.w = 0.9f, .h = 1.15f, .color = 0xBDE0FF}},
};

constexpr std::array<std::string_view, kPresets.size()> kNames = [] {
    std::array<std::string_view, kPresets.size()> n{};
    for (size_t i = 0; i < kPresets.size(); i++) {
        n[i] = kPresets[i].name;
    }
    return n;
}();

constexpr uint32_t kBlinkMs = 160;

float approach(float cur, float target, float k)
{
    return cur + (target - cur) * k;
}

uint32_t mix_color(uint32_t a, uint32_t b, float k)
{
    uint32_t out = 0;
    for (int s = 0; s <= 16; s += 8) {
        const float ca = static_cast<float>((a >> s) & 0xFF), cb = static_cast<float>((b >> s) & 0xFF);
        out |= static_cast<uint32_t>(std::lround(ca + (cb - ca) * k)) << s;
    }
    return out;
}

} // namespace

std::span<const std::string_view> moods()
{
    return kNames;
}

const EyeShape &preset(std::string_view mood)
{
    for (const auto &p : kPresets) {
        if (p.name == mood) {
            return p.shape;
        }
    }
    return kPresets[0].shape;
}

EyeAnimator::EyeAnimator(uint32_t seed) : rng_(seed) {}

float EyeAnimator::random(float lo, float hi)
{
    return std::uniform_real_distribution<float>(lo, hi)(rng_);
}

void EyeAnimator::set_mood(std::string_view mood)
{
    for (auto name : kNames) {
        if (name == mood) {
            mood_ = name; // keep the preset's own storage, not the caller's
            frozen_ = false;
            return;
        }
    }
    mood_ = kNames[0];
    frozen_ = false;
}

void EyeAnimator::snap()
{
    cur_ = preset(mood_);
    look_x_ = look_y_ = aim_x_ = aim_y_ = 0;
    blinking_ = false;
    jitter_ = 0;
    frozen_ = true;
}

void EyeAnimator::update(uint32_t now_ms)
{
    const uint32_t dt = last_ == 0 ? 16 : now_ms - last_;
    last_ = now_ = now_ms;
    if (frozen_) {
        return;
    }
    // The emulator moves 15 % of the way per 60 Hz frame; the same speed for any frame rate.
    const float k_shape = 1.0f - std::pow(0.85f, static_cast<float>(dt) / 16.7f);
    const float k_look = 1.0f - std::pow(0.80f, static_cast<float>(dt) / 16.7f);

    const EyeShape &t = preset(mood_);
    cur_.w = approach(cur_.w, t.w, k_shape);
    cur_.h = approach(cur_.h, t.h, k_shape);
    cur_.top = approach(cur_.top, t.top, k_shape);
    cur_.tilt = approach(cur_.tilt, t.tilt, k_shape);
    cur_.bottom = approach(cur_.bottom, t.bottom, k_shape);
    cur_.dy = approach(cur_.dy, t.dy, k_shape);
    cur_.asym = approach(cur_.asym, t.asym, k_shape);
    cur_.color = mix_color(cur_.color, t.color, k_shape);

    if (now_ms >= next_glance_) {
        const bool calm = mood_ == "sleepy";
        aim_x_ = random(-0.5f, 0.5f) * (calm ? 20.0f : 60.0f);
        aim_y_ = random(-0.5f, 0.5f) * (calm ? 10.0f : 36.0f);
        if (random(0, 1) < 0.35f) {
            aim_x_ = aim_y_ = 0;
        }
        next_glance_ = now_ms + static_cast<uint32_t>(random(1200, 4000));
    }
    if (mood_ == "dizzy") {
        aim_x_ = std::cos(static_cast<float>(now_ms) / 180.0f) * 28.0f;
        aim_y_ = std::sin(static_cast<float>(now_ms) / 180.0f) * 18.0f;
    }
    look_x_ = approach(look_x_, aim_x_, k_look);
    look_y_ = approach(look_y_, aim_y_, k_look);

    if (!blinking_ && now_ms >= next_blink_) {
        blinking_ = true;
        blink_start_ = now_ms;
    }
    if (blinking_ && now_ms - blink_start_ > kBlinkMs) {
        blinking_ = false;
        next_blink_ = now_ms + static_cast<uint32_t>(random(2000, 6000));
    }
    jitter_ = mood_ == "cold" ? random(-1.2f, 1.2f) : 0.0f;
}

EyeFrame EyeAnimator::frame() const
{
    EyeFrame f;
    f.shape = cur_;
    f.look_x = look_x_;
    f.look_y = look_y_;
    f.jitter = jitter_;
    f.time_ms = frozen_ ? 0 : now_;
    if (blinking_) {
        const float p = static_cast<float>(now_ - blink_start_) / kBlinkMs;
        f.blink = std::max(0.06f, std::abs(1.0f - 2.0f * p));
    }
    return f;
}

EyeRect eye_rect(const EyeFrame &f, int side, float cx, float cy, float scale)
{
    const EyeShape &s = f.shape;
    const float asym = side == 1 ? 1 + s.asym : 1 - s.asym * 0.4f;
    const float w = 108 * s.w * scale;
    const float h = 132 * s.h * scale * f.blink * asym;
    const float x = cx + side * 92 * scale + f.look_x * scale - w / 2 + f.jitter;
    const float y = cy + (s.dy + f.look_y) * scale - h / 2;
    return {x, y, w, h, std::min(30 * scale, std::min(w, h) / 2)};
}

} // namespace ui
