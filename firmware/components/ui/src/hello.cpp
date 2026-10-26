#include "ui/hello.h"

#include <initializer_list>

#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/theme.h"

namespace ui {
namespace {

struct State {
    lv_obj_t *dot = nullptr;
    lv_obj_t *touch_label = nullptr;
    int taps = 0;
};
State g;

lv_obj_t *make_eye(lv_obj_t *parent, int x)
{
    lv_obj_t *eye = lv_obj_create(parent);
    lv_obj_remove_style_all(eye);
    lv_obj_set_size(eye, 72, 104);
    lv_obj_set_style_radius(eye, 30, 0);
    lv_obj_set_style_bg_color(eye, theme::accent(), 0);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    lv_obj_align(eye, LV_ALIGN_CENTER, x, -46);
    lv_obj_set_clickable(eye, false);
    return eye;
}

lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}

void on_touch(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_obj_set_hidden(g.dot, false);
    lv_obj_set_pos(g.dot, p.x - 8, p.y - 8);
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        g.taps++;
    }
    lv_label_set_text_fmt(g.touch_label, "touch %d, %d  ·  taps %d", static_cast<int>(p.x), static_cast<int>(p.y),
                          g.taps);
}

} // namespace

void create_hello(lv_obj_t *parent, const HelloInfo &info)
{
    g = State{};
    lv_obj_set_style_bg_color(parent, theme::background(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(parent, false);

    // Ring along the edge: shows whether the round screen is aligned and fully visible.
    lv_obj_t *rim = lv_arc_create(parent);
    lv_obj_set_size(rim, lv_pct(100), lv_pct(100));
    lv_obj_center(rim);
    lv_arc_set_bg_angles(rim, 0, 360);
    lv_arc_set_value(rim, 0);
    lv_obj_set_style_arc_color(rim, theme::rim(), LV_PART_MAIN);
    lv_obj_set_style_arc_width(rim, theme::kRimWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(rim, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(rim, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_clickable(rim, false);

    make_eye(parent, -56);
    make_eye(parent, 56);

    lv_obj_t *title = make_label(parent, font::title(), theme::text(), 50);
    lv_label_set_text(title, "Blinky");

    lv_obj_t *about = make_label(parent, font::small(), theme::muted(), 88);
    lv_label_set_text_fmt(about, "fw %s  ·  %s  ·  LVGL %d.%d", info.version, info.board, LVGL_VERSION_MAJOR,
                          LVGL_VERSION_MINOR);

    g.touch_label = make_label(parent, font::body(), theme::warn(), 128);
    lv_label_set_text(g.touch_label, "touch the screen");

    g.dot = lv_obj_create(parent);
    lv_obj_remove_style_all(g.dot);
    lv_obj_set_size(g.dot, 16, 16);
    lv_obj_set_style_radius(g.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g.dot, theme::warn(), 0);
    lv_obj_set_style_bg_opa(g.dot, LV_OPA_COVER, 0);
    lv_obj_set_hidden(g.dot, true);
    lv_obj_set_clickable(g.dot, false);

    // Icon row: shows that the icon font works. One label per icon (the icon font has no
    // space), in a row narrow enough for the circle at this height.
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_clickable(row, false);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 160);
    for (const char *sym : {icon::kSun, icon::kPartlyCloudy, icon::kRain, icon::kWifi, icon::kUsb, icon::kAlarm,
                            icon::kBattery}) {
        lv_obj_t *l = lv_label_create(row);
        lv_obj_set_style_text_font(l, font::icons(), 0);
        lv_obj_set_style_text_color(l, theme::muted(), 0);
        lv_label_set_text(l, sym);
    }

    lv_obj_add_event_cb(parent, on_touch, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(parent, on_touch, LV_EVENT_CLICKED, nullptr);
}

int touches()
{
    return g.taps;
}

} // namespace ui
