#include "ui/clock_page.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/fonts.h"
#include "ui/round_layout.h"
#include "ui/theme.h"
#include "set.h"

namespace ui {
namespace {

// Sizes on the 412 px design; the face is centred on larger screens.
constexpr float kCentre = kDesignSize / 2.0f;
constexpr float kPi = 3.14159265f;
constexpr float kMarkOuter = 170, kMarkInner = 152, kQuarterInner = 140;
constexpr float kHourLen = 90, kMinuteLen = 135, kSecondLen = 150, kSecondTail = 24;

constexpr const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// point returns the point at radius r and angle a (in turns, 0 = 12 o'clock, clockwise).
lv_point_precise_t point(float a, float r)
{
    const float rad = a * 2 * kPi;
    return {static_cast<lv_value_precise_t>(kCentre + r * std::sin(rad)),
            static_cast<lv_value_precise_t>(kCentre - r * std::cos(rad))};
}

lv_obj_t *line(lv_obj_t *parent, int width, lv_color_t color)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_clickable(l, false);
    return l;
}

} // namespace

ClockPage *ClockPage::create(lv_obj_t *parent, LocalTime local_time)
{
    auto *c = new ClockPage(std::move(local_time));
    lv_obj_add_event_cb(parent, on_delete, LV_EVENT_DELETE, c);

    // The face: a 412×412 box in the middle of the screen; touches go to the screen.
    lv_obj_t *face = lv_obj_create(parent);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, kDesignSize, kDesignSize);
    lv_obj_center(face);
    lv_obj_set_clickable(face, false);

    // The marks never move, so their points can be static.
    static std::array<std::array<lv_point_precise_t, 2>, 12> marks;
    for (int i = 0; i < 12; i++) {
        const bool quarter = i % 3 == 0;
        marks[i] = {point(i / 12.0f, quarter ? kQuarterInner : kMarkInner), point(i / 12.0f, kMarkOuter)};
        lv_line_set_points(line(face, quarter ? 6 : 3, quarter ? theme::text() : theme::muted()), marks[i].data(), 2);
    }

    c->date_ = lv_label_create(face);
    lv_obj_set_style_text_font(c->date_, font::body(), 0);
    set_text_color(c->date_, theme::muted());
    lv_obj_align(c->date_, LV_ALIGN_CENTER, 0, 64);

    c->hour_.line = line(face, 9, theme::text());
    c->minute_.line = line(face, 6, theme::text());
    c->second_.line = line(face, 2, theme::accent());

    c->centre_ = lv_obj_create(face);
    lv_obj_remove_style_all(c->centre_);
    lv_obj_set_size(c->centre_, 14, 14);
    lv_obj_set_style_radius(c->centre_, LV_RADIUS_CIRCLE, 0);
    set_bg_color(c->centre_, theme::accent());
    lv_obj_set_style_bg_opa(c->centre_, LV_OPA_COVER, 0);
    lv_obj_center(c->centre_);
    lv_obj_set_clickable(c->centre_, false);

    c->refresh();
    return c;
}

void ClockPage::refresh()
{
    std::tm t{};
    const bool have = local_time_ && local_time_(t);
    set_line_color(second_.line, theme::accent()); // ui.color may have changed
    set_bg_color(centre_, theme::accent());
    for (lv_obj_t *o : {hour_.line, minute_.line, second_.line, centre_, date_}) {
        lv_obj_set_hidden(o, !have);
    }
    if (!have) {
        return;
    }
    const float s = static_cast<float>(t.tm_sec) / 60;
    const float m = (static_cast<float>(t.tm_min) + s) / 60;
    const float h = (static_cast<float>(t.tm_hour % 12) + m) / 12;
    hour_.move(point(h, 0), point(h, kHourLen));
    minute_.move(point(m, 0), point(m, kMinuteLen));
    second_.move(point(s + 0.5f, kSecondTail), point(s, kSecondLen));

    char buf[32];
    std::snprintf(buf, sizeof buf, "%s %d %s", kDays[t.tm_wday % 7], t.tm_mday, kMonths[t.tm_mon % 12]);
    set_text(date_, buf);
}

// move puts the hand on a → b. The line object covers only the hand's own box (not the face
// from its corner), so a move redraws two small areas, and a hand that stays (hour and
// minute, most of the time) redraws nothing.
void ClockPage::Hand::move(lv_point_precise_t a, lv_point_precise_t b)
{
    if (at[0].x == a.x && at[0].y == a.y && at[1].x == b.x && at[1].y == b.y) {
        return;
    }
    at = {a, b};
    const int32_t pad = lv_obj_get_style_line_width(line, LV_PART_MAIN) / 2 + 2;
    const auto x0 = static_cast<int32_t>(std::floor(std::min(a.x, b.x))) - pad;
    const auto y0 = static_cast<int32_t>(std::floor(std::min(a.y, b.y))) - pad;
    lv_obj_set_pos(line, x0, y0);
    points = {lv_point_precise_t{a.x - x0, a.y - y0}, lv_point_precise_t{b.x - x0, b.y - y0}};
    lv_line_set_points(line, points.data(), 2); // the line keeps a pointer to points
}

void ClockPage::on_delete(lv_event_t *e)
{
    delete static_cast<ClockPage *>(lv_event_get_user_data(e));
}

} // namespace ui
