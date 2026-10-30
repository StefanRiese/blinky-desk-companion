// The mood eyes as an LVGL object: draws EyeAnimator frames and animates them with an
// LVGL timer. Only the area around the eyes is redrawn each frame.
#pragma once

#include <string_view>

#include "lvgl.h"
#include "ui/eye_model.h"

namespace ui {

class Eyes {
public:
    // create fills parent with the eyes (centred, slightly below the middle like the
    // emulator). The object is deleted with its parent.
    static Eyes *create(lv_obj_t *parent, uint32_t seed);

    lv_obj_t *obj() const { return obj_; }
    void set_mood(std::string_view mood) { anim_.set_mood(mood); }
    void set_color(uint32_t rgb) { anim_.set_color(rgb); }
    std::string_view mood() const { return anim_.mood(); }

    // set_idle plays the idle routines (looking around, double blinks) while nobody touches
    // the screen.
    void set_idle(bool idle) { anim_.set_idle(idle); }

    // snap shows the current mood at once and stops the animation (screenshots, tests).
    void snap();

    // glow draws a soft halo around the eyes (an LVGL shadow: expensive, so off by default).
    void set_glow(bool on);

private:
    explicit Eyes(uint32_t seed) : anim_(seed) {}
    static void on_event(lv_event_t *e);
    static void on_timer(lv_timer_t *t);
    void draw(lv_layer_t *layer);
    void draw_extras(lv_layer_t *layer, const EyeFrame &f, float cx, float cy);
    lv_area_t bounds() const;

    lv_obj_t *obj_ = nullptr;
    lv_timer_t *timer_ = nullptr;
    EyeAnimator anim_;
    lv_area_t last_{};
    bool glow_ = false;
};

} // namespace ui
