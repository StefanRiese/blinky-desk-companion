// The mood eyes as numbers: presets per mood, smooth transitions, blinking and glances.
// Same model and values as the browser emulator (host/internal/sim/web/index.html), so
// both show the same eyes. No LVGL here; drawing is in eyes.h.
#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <span>
#include <string_view>

namespace ui {

// The base eye in px of the 412 px design: an upright pill in the proportions of the README's
// GIF and the app icon (host/assets/make_icon.py: 40×64, radius 18, ±32 apart, 8 above the
// middle on a 224 px face), 15 % larger than that on the screen. Keep in sync with drawEyes
// in host/internal/sim/web/index.html.
constexpr float kEyeW = 85, kEyeH = 136, kEyeRadius = 38;
constexpr float kEyeApart = 68; // from the middle of the screen to the middle of each eye
constexpr float kEyeRaise = 15; // the eyes sit this much above the middle

// EyeShape is one eye expression. Sizes are relative to the base eye (kEyeW × kEyeH).
struct EyeShape {
    float w = 1, h = 1;    // width and height factors
    float top = 0;         // upper lid, part of the eye height it covers (0…1)
    float tilt = 0;        // lid slope: > 0 outer corner lower (sad), < 0 inner corner lower (angry)
    float bottom = 0;      // "cheek" pushing up from below (happy), part of the height
    float dy = 0;          // vertical offset in px
    float asym = 0;        // right eye taller (> 0) or smaller (< 0) than the left
    uint32_t color = 0x3EE8FF;
};

// moods lists the known mood names; preset returns the shape for one (neutral if unknown).
std::span<const std::string_view> moods();
const EyeShape &preset(std::string_view mood);

// EyeFrame is what to draw at one moment.
struct EyeFrame {
    EyeShape shape;
    float look_x = 0, look_y = 0; // gaze offset in px
    float blink = 1;              // 1 = open … ~0 = closed
    float jitter = 0;             // horizontal shiver in px (cold)
    uint32_t time_ms = 0;         // animation time, for the extras (sweat drop, "z")
};

// EyeAnimator moves the eyes towards the current mood's preset and adds life: blinks every
// 2–6 s, glances every 1.2–4 s, a circling gaze when dizzy, a shiver when cold. While idle
// (nobody touched the screen for a while) it plays little routines like the README GIF
// instead of the glances: a slow look right and left, double blinks, a look up to the side.
class EyeAnimator {
public:
    explicit EyeAnimator(uint32_t seed = 1);

    void set_mood(std::string_view mood);
    std::string_view mood() const { return mood_; }

    // set_color sets the eyes' own colour (ui.color); moods with a colour of their own keep it.
    void set_color(uint32_t rgb) { color_ = rgb; }

    // set_idle switches the idle routines on or off; off stops a routine at once.
    void set_idle(bool idle);
    bool idle() const { return idle_; }

    // update advances to now_ms (call every frame); frame returns what to draw.
    void update(uint32_t now_ms);
    EyeFrame frame() const;

    // snap jumps straight to the preset, eyes open and looking ahead, and stops blinking
    // and glancing until the next set_mood (for screenshots and tests).
    void snap();

private:
    // Step is one part of an idle routine: look at (x, y), maybe blink, then wait ms.
    struct Step {
        float x = 0, y = 0;
        uint32_t ms = 0;
        bool blink = false;
    };

    float random(float lo, float hi);
    void glance(uint32_t now_ms);
    void plan_routine();

    std::array<Step, 6> steps_{};
    size_t step_count_ = 0, step_ = 0;
    bool idle_ = false;

    std::mt19937 rng_;
    EyeShape target() const; // the mood's preset in the eyes' colour
    std::string_view mood_ = "neutral";
    uint32_t color_ = EyeShape{}.color;
    EyeShape cur_ = preset("neutral");
    float look_x_ = 0, look_y_ = 0;   // current gaze
    float aim_x_ = 0, aim_y_ = 0;     // where the gaze is heading
    uint32_t next_glance_ = 1200;
    uint32_t blink_start_ = 0, next_blink_ = 2500;
    bool blinking_ = false;
    bool frozen_ = false;
    uint32_t last_ = 0, now_ = 0;
    float jitter_ = 0;
};

// EyeLayout places both eyes for a screen centre (cx, cy) and scale; used by the renderer
// and the tests.
struct EyeRect {
    float x, y, w, h, radius;
};
EyeRect eye_rect(const EyeFrame &f, int side /* -1 left, +1 right */, float cx, float cy, float scale);

} // namespace ui
