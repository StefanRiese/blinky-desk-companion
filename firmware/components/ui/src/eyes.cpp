#include "ui/eyes.h"

#include <algorithm>
#include <cmath>

#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/theme.h"

namespace ui {
namespace {

constexpr uint32_t kFrameMs = 33; // ~30 FPS
constexpr int kGlow = 18;         // halo width in px

lv_point_precise_t pt(float x, float y)
{
    return {static_cast<lv_value_precise_t>(x), static_cast<lv_value_precise_t>(y)};
}

void triangle(lv_layer_t *layer, lv_color_t color, lv_point_precise_t a, lv_point_precise_t b, lv_point_precise_t c)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = color;
    d.opa = LV_OPA_COVER;
    d.p[0] = a;
    d.p[1] = b;
    d.p[2] = c;
    lv_draw_triangle(layer, &d);
}

} // namespace

Eyes *Eyes::create(lv_obj_t *parent, uint32_t seed)
{
    auto *self = new Eyes(seed);
    self->obj_ = lv_obj_create(parent);
    lv_obj_remove_style_all(self->obj_);
    lv_obj_set_size(self->obj_, lv_pct(100), lv_pct(100));
    lv_obj_center(self->obj_);
    lv_obj_set_clickable(self->obj_, false); // touches go to the screen
    lv_obj_add_event_cb(self->obj_, on_event, LV_EVENT_ALL, self);
    self->timer_ = lv_timer_create(on_timer, kFrameMs, self);
    return self;
}

void Eyes::snap()
{
    anim_.snap();
    lv_obj_invalidate(obj_);
}

void Eyes::set_glow(bool on)
{
    glow_ = on;
    lv_obj_invalidate(obj_);
}

void Eyes::on_event(lv_event_t *e)
{
    auto *self = static_cast<Eyes *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_DRAW_MAIN:
        self->draw(lv_event_get_layer(e));
        break;
    case LV_EVENT_DELETE:
        lv_timer_delete(self->timer_);
        delete self;
        break;
    default:
        break;
    }
}

void Eyes::on_timer(lv_timer_t *t)
{
    auto *self = static_cast<Eyes *>(lv_timer_get_user_data(t));
    self->anim_.update(lv_tick_get());
    // Redraw where the eyes were and where they are now, nothing else.
    const lv_area_t now = self->bounds();
    lv_area_t both = now;
    if (self->last_.x2 > self->last_.x1) {
        both = {std::min(now.x1, self->last_.x1), std::min(now.y1, self->last_.y1), std::max(now.x2, self->last_.x2),
                std::max(now.y2, self->last_.y2)};
    }
    lv_obj_invalidate_area(self->obj_, &both);
    self->last_ = now;
}

// bounds is the screen area the current frame covers, with room for the glow.
lv_area_t Eyes::bounds() const
{
    lv_area_t c;
    lv_obj_get_coords(obj_, &c);
    const float cx = (c.x1 + c.x2 + 1) / 2.0f, cy = (c.y1 + c.y2 + 1) / 2.0f + 12;
    const EyeFrame f = anim_.frame();
    const EyeRect l = eye_rect(f, -1, cx, cy, 1), r = eye_rect(f, 1, cx, cy, 1);
    const int m = (glow_ ? kGlow : 0) + 6;
    lv_area_t a = {static_cast<int32_t>(std::floor(l.x)) - m, static_cast<int32_t>(std::floor(std::min(l.y, r.y))) - m,
                   static_cast<int32_t>(std::ceil(r.x + r.w)) + m,
                   static_cast<int32_t>(std::ceil(std::max(l.y + l.h, r.y + r.h))) + m};
    const std::string_view mood = anim_.mood();
    if (mood == "hot" || mood == "sleepy" || mood == "love") { // extras above and beside the eyes
        a.x2 = std::max(a.x2, static_cast<int32_t>(cx) + 175);
        a.y1 = std::min(a.y1, static_cast<int32_t>(cy) - 160);
    }
    return a;
}

void Eyes::draw(lv_layer_t *layer)
{
    lv_area_t c;
    lv_obj_get_coords(obj_, &c);
    const float cx = (c.x1 + c.x2 + 1) / 2.0f, cy = (c.y1 + c.y2 + 1) / 2.0f + 12;
    const EyeFrame f = anim_.frame();
    const EyeShape &s = f.shape;
    const lv_color_t color = lv_color_hex(s.color);
    const lv_color_t bg = theme::background();

    for (int side : {-1, 1}) {
        const EyeRect e = eye_rect(f, side, cx, cy, 1);
        const lv_area_t area = {static_cast<int32_t>(std::lround(e.x)), static_cast<int32_t>(std::lround(e.y)),
                                static_cast<int32_t>(std::lround(e.x + e.w)) - 1,
                                static_cast<int32_t>(std::lround(e.y + e.h)) - 1};
        const auto radius = static_cast<int32_t>(e.radius);

        // 1. The glow, outside the eye only (an empty rectangle with a shadow).
        if (glow_) {
            lv_draw_rect_dsc_t g;
            lv_draw_rect_dsc_init(&g);
            g.bg_opa = LV_OPA_TRANSP;
            g.radius = radius;
            g.shadow_color = color;
            g.shadow_width = kGlow;
            g.shadow_opa = LV_OPA_50;
            lv_draw_rect(layer, &g, &area);
        }

        // 2. The eye with lid and cheek on its own layer, masked to the eye's rounded shape,
        //    so the lid never paints over the glow (as the emulator's clip does).
        lv_layer_t *eye = lv_draw_layer_create(layer, LV_COLOR_FORMAT_ARGB8888, &area);
        lv_draw_rect_dsc_t body;
        lv_draw_rect_dsc_init(&body);
        body.bg_color = color;
        body.bg_opa = LV_OPA_COVER;
        lv_draw_rect(eye, &body, &area);

        // Upper lid: a rectangle down to its highest corner plus one triangle for the slope.
        // The rectangle overlaps the triangle by a pixel so no seam shows between them.
        if (s.top > 0.01f) {
            const float base = e.y + s.top * e.h, d = s.tilt * e.h * 0.4f;
            const float outer = side == -1 ? e.x : e.x + e.w, inner = side == -1 ? e.x + e.w : e.x;
            const float outer_y = base + d, inner_y = base - d, high = std::min(outer_y, inner_y);
            lv_draw_rect_dsc_t lid;
            lv_draw_rect_dsc_init(&lid);
            lid.bg_color = bg;
            lid.bg_opa = LV_OPA_COVER;
            const lv_area_t band = {area.x1, area.y1, area.x2, static_cast<int32_t>(std::lround(high)) + 1};
            if (band.y2 >= band.y1) {
                lv_draw_rect(eye, &lid, &band);
            }
            if (std::abs(outer_y - inner_y) > 0.5f) {
                const float low_x = outer_y > inner_y ? outer : inner, high_x = outer_y > inner_y ? inner : outer;
                triangle(eye, bg, pt(low_x, high), pt(low_x, std::max(outer_y, inner_y)), pt(high_x, high));
            }
        }
        // "Cheek" pushing up from below (happy): a large circle whose top follows the
        // ellipse of the emulator closely across the eye's width.
        if (s.bottom > 0.01f) {
            const float rx = e.w * 0.85f, ry = e.h * 0.55f;
            const float top = e.y + e.h - s.bottom * e.h; // highest point of the ellipse
            const float r = rx * rx / ry;                 // same curvature at the top
            lv_draw_rect_dsc_t cheek;
            lv_draw_rect_dsc_init(&cheek);
            cheek.bg_color = bg;
            cheek.bg_opa = LV_OPA_COVER;
            cheek.radius = LV_RADIUS_CIRCLE;
            const float mid = e.x + e.w / 2;
            const lv_area_t circle = {static_cast<int32_t>(std::lround(mid - r)), static_cast<int32_t>(std::lround(top)),
                                      static_cast<int32_t>(std::lround(mid + r)),
                                      static_cast<int32_t>(std::lround(top + 2 * r))};
            lv_draw_rect(eye, &cheek, &circle);
        }

        lv_draw_mask_rect_dsc_t mask;
        lv_draw_mask_rect_dsc_init(&mask);
        mask.area = area;
        mask.radius = radius;
        lv_draw_mask_rect(eye, &mask);

        lv_draw_image_dsc_t img;
        lv_draw_image_dsc_init(&img);
        img.src = eye;
        lv_draw_layer(layer, &img, &area);
    }
    draw_extras(layer, f, cx, cy);
}

// draw_extras adds the emulator's small mood signs: a sweat drop running down (hot), a
// rising, fading "z" (sleepy) and a heart above the eyes (love).
void Eyes::draw_extras(lv_layer_t *layer, const EyeFrame &f, float cx, float cy)
{
    const std::string_view mood = anim_.mood();
    if (mood == "hot") {
        const float x = cx + 150, y = cy - 70 + static_cast<float>((f.time_ms / 25) % 40);
        const lv_color_t blue = lv_color_hex(0x7DD3FC);
        lv_draw_rect_dsc_t drop;
        lv_draw_rect_dsc_init(&drop);
        drop.bg_color = blue;
        drop.bg_opa = LV_OPA_COVER;
        drop.radius = LV_RADIUS_CIRCLE;
        const lv_area_t c = {static_cast<int32_t>(x - 7), static_cast<int32_t>(y - 7), static_cast<int32_t>(x + 7),
                             static_cast<int32_t>(y + 7)};
        lv_draw_rect(layer, &drop, &c);
        triangle(layer, blue, pt(x - 7, y - 2), pt(x, y - 16), pt(x + 7, y - 2));
    } else if (mood == "sleepy") {
        const float t = static_cast<float>(f.time_ms % 1200) / 1200.0f;
        lv_draw_label_dsc_t z;
        lv_draw_label_dsc_init(&z);
        z.text = "z";
        z.font = t < 0.5f ? font::body() : font::title();
        z.color = lv_color_hex(0xC8DCFF);
        z.opa = static_cast<lv_opa_t>((1.0f - t) * 255);
        const auto x = static_cast<int32_t>(cx + 120 + t * 30), y = static_cast<int32_t>(cy - 100 - t * 40);
        const lv_area_t a = {x - 15, y - 20, x + 15, y + 20};
        lv_draw_label(layer, &z, &a);
    } else if (mood == "love") {
        lv_draw_label_dsc_t heart;
        lv_draw_label_dsc_init(&heart);
        heart.text = icon::kHeart;
        heart.font = font::icons();
        heart.color = lv_color_hex(0xFF6FA8);
        heart.align = LV_TEXT_ALIGN_CENTER;
        const auto x = static_cast<int32_t>(cx), y = static_cast<int32_t>(cy - 132);
        const lv_area_t a = {x - 20, y - 14, x + 20, y + 14};
        lv_draw_label(layer, &heart, &a);
    }
}

} // namespace ui
