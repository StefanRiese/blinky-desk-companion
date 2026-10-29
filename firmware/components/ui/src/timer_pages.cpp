#include "ui/timer_pages.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>

#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/round_layout.h"
#include "ui/theme.h"
#include "set.h"

namespace ui {
namespace {

constexpr int kC = 206; // screen centre
const lv_color_t kRed = lv_color_hex(0xF87171);
const lv_color_t kAmber = lv_color_hex(0xFBBF24);

lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, lv_pct(100), lv_pct(100));
    lv_obj_set_clickable(b, false);
    return b;
}

lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *s = "")
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    set_text_color(l, color);
    set_text(l, s);
    return l;
}

void title(lv_obj_t *parent, const char *s)
{
    lv_obj_t *t = text(parent, font::small(), theme::muted(), s);
    lv_obj_set_style_text_letter_space(t, 2, 0);
    place(t, LV_ALIGN_TOP_MID, 0, 86);
}

// big_time places "AA:BB" with the colon in the middle and the two numbers right/left of
// it, so the arrows above and below line up with each number.
void big_time(lv_obj_t *parent, const lv_font_t *font, int y, lv_obj_t *&a, lv_obj_t *&colon, lv_obj_t *&b)
{
    a = text(parent, font, theme::text(), "00");
    place(a, LV_ALIGN_TOP_RIGHT, -(412 - (kC - 14)), y);
    colon = text(parent, font, theme::text(), ":");
    place(colon, LV_ALIGN_TOP_MID, 0, y - 4);
    b = text(parent, font, theme::text(), "00");
    place(b, LV_ALIGN_TOP_LEFT, kC + 14, y);
}

// arrows adds ↑ above and ↓ below both numbers (x = centre ± dx), plus optional labels.
void arrows(lv_obj_t *parent, int dx, int y_up, int y_down, const char *left, const char *right, int y_label)
{
    for (int side : {-1, 1}) {
        lv_obj_t *up = text(parent, font::body(), lv_color_hex(0x4B5563), "↑");
        place(up, LV_ALIGN_TOP_MID, side * dx, y_up);
        lv_obj_t *down = text(parent, font::body(), lv_color_hex(0x4B5563), "↓");
        place(down, LV_ALIGN_TOP_MID, side * dx, y_down);
        if (left != nullptr) {
            lv_obj_t *l = text(parent, font::small(), lv_color_hex(0x6B7280), side < 0 ? left : right);
            place(l, LV_ALIGN_TOP_MID, side * dx, y_label);
        }
    }
}

void set_color(std::initializer_list<lv_obj_t *> objs, lv_color_t c)
{
    for (lv_obj_t *o : objs) {
        set_text_color(o, c);
    }
}

bool flash_on(uint32_t now_ms)
{
    return (now_ms / 400) % 2 == 0;
}

} // namespace

// ---------------------------------------------------------------- countdown

CountdownPage *CountdownPage::create(lv_obj_t *parent, const core::Countdown &model, Gestures g)
{
    auto *p = new CountdownPage(model, g);
    lv_obj_t *root = box(parent);
    title(root, "COUNTDOWN");

    // Progress ring along the edge: the part of the set time that is left.
    p->ring_ = lv_arc_create(root);
    lv_obj_set_size(p->ring_, screen_size(root) - 20, screen_size(root) - 20);
    lv_obj_center(p->ring_);
    lv_arc_set_rotation(p->ring_, 270);
    lv_arc_set_bg_angles(p->ring_, 0, 360);
    lv_arc_set_range(p->ring_, 0, 1000);
    lv_obj_set_style_arc_width(p->ring_, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(p->ring_, 6, LV_PART_INDICATOR);
    set_arc_color(p->ring_, theme::rim());
    lv_obj_set_style_arc_rounded(p->ring_, false, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(p->ring_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_clickable(p->ring_, false);

    big_time(root, font::digits_large(), 150, p->mm_, p->colon_, p->ss_);
    p->edit_ = box(root);
    arrows(p->edit_, 70, 118, 258, "min", "sec", 290);
    p->hint_ = text(root, font::small(), theme::muted());
    place(p->hint_, LV_ALIGN_TOP_MID, 0, 324);
    p->refresh(0);
    return p;
}

void CountdownPage::refresh(uint32_t now_ms)
{
    const core::Countdown::View v = model_.view(now_ms);
    const uint32_t secs = (v.left_ms + 999) / 1000;
    set_text_fmt(mm_, "%02u", static_cast<unsigned>(secs / 60));
    set_text_fmt(ss_, "%02u", static_cast<unsigned>(secs % 60));

    lv_color_t c = theme::text();
    if (v.ringing) {
        c = flash_on(now_ms) ? kRed : theme::text();
    } else if (v.running) {
        c = theme::accent();
    }
    set_color({mm_, colon_, ss_}, c);
    lv_obj_set_hidden(edit_, v.running || v.ringing);

    const bool show_ring = !v.ringing && v.set_ms > 0 && (v.running || v.left_ms > 0);
    lv_obj_set_hidden(ring_, !show_ring);
    if (show_ring) {
        lv_arc_set_value(ring_, static_cast<int32_t>(1000ULL * v.left_ms / v.set_ms));
        set_arc_color(ring_, v.running ? theme::accent() : lv_color_hex(0x6B7280), LV_PART_INDICATOR);
    }

    const char *go = g_.shake ? "shake" : "tap";
    char hint[64];
    if (v.ringing) {
        std::snprintf(hint, sizeof hint, "Time's up!  ·  %s to stop", go);
    } else if (v.running) {
        std::snprintf(hint, sizeof hint, "%s to pause", go);
    } else if (v.paused()) {
        std::snprintf(hint, sizeof hint, "paused  ·  %s to resume", go);
    } else if (v.left_ms > 0) {
        std::snprintf(hint, sizeof hint, "%s to start  ·  hold to reset", go);
    } else {
        std::snprintf(hint, sizeof hint, "swipe ↑↓ on min / sec to set");
    }
    set_text(hint_, hint);
    set_text_color(hint_, v.ringing ? kRed : theme::muted());
}

// ---------------------------------------------------------------- alarm

AlarmPage *AlarmPage::create(lv_obj_t *parent, const core::AlarmClock &model, Gestures g)
{
    auto *p = new AlarmPage(model, g);
    lv_obj_t *root = box(parent);
    title(root, "ALARM");

    // One alarm: time with arrows, weekdays, on/off badge, next alarm.
    p->normal_ = box(root);
    big_time(p->normal_, font::digits(), 140, p->hh_, p->colon_, p->mm_);
    arrows(p->normal_, 66, 112, 222, nullptr, nullptr, 0);
    for (int i = 0; i < 7; i++) { // Monday first
        static constexpr const char *kLetters[] = {"M", "T", "W", "T", "F", "S", "S"};
        p->days_[i] = text(p->normal_, font::small(), theme::muted(), kLetters[i]);
        place(p->days_[i], LV_ALIGN_TOP_MID, -84 + i * 28, 252);
    }
    p->badge_ = lv_obj_create(p->normal_);
    lv_obj_remove_style_all(p->badge_);
    lv_obj_set_size(p->badge_, 68, 26);
    lv_obj_set_style_radius(p->badge_, 13, 0);
    lv_obj_set_style_bg_opa(p->badge_, LV_OPA_COVER, 0);
    place(p->badge_, LV_ALIGN_TOP_MID, 0, 280);
    p->badge_text_ = text(p->badge_, font::small(), theme::muted(), "OFF");
    lv_obj_center(p->badge_text_);
    p->info_ = text(p->normal_, font::small(), theme::muted());
    place(p->info_, LV_ALIGN_TOP_MID, 0, 320);

    // While ringing: alarm icon, flashing time, how to stop or snooze.
    p->ringing_ = box(root);
    place(text(p->ringing_, font::icons_large(), theme::text(), icon::kAlarm), LV_ALIGN_TOP_MID, 0, 118);
    p->ring_time_ = text(p->ringing_, font::digits(), theme::text());
    place(p->ring_time_, LV_ALIGN_TOP_MID, 0, 176);
    p->ring_hint_ = text(p->ringing_, font::small(), kAmber);
    place(p->ring_hint_, LV_ALIGN_TOP_MID, 0, 280);

    p->empty_ = box(root);
    lv_obj_align(text(p->empty_, font::body(), lv_color_hex(0x6B7280), "no alarms"), LV_ALIGN_CENTER, 0, -10);
    lv_obj_align(text(p->empty_, font::small(), lv_color_hex(0x6B7280), "add them in the setup page"), LV_ALIGN_CENTER,
                 0, 20);
    p->refresh(0, nullptr);
    return p;
}

void AlarmPage::refresh(uint32_t now_ms, const std::tm *local)
{
    const auto list = model_.alarms();
    const bool ringing = model_.ringing();
    lv_obj_set_hidden(ringing_, !ringing);
    lv_obj_set_hidden(normal_, ringing || list.empty());
    lv_obj_set_hidden(empty_, ringing || !list.empty());

    if (ringing) {
        set_text(ring_time_, model_.ring_at().c_str());
        set_text_color(ring_time_, flash_on(now_ms) ? kAmber : theme::text());
        set_text(ring_hint_, g_.shake ? "shake = stop  ·  tap = snooze" : "tap = snooze  ·  hold = stop");
        return;
    }
    if (list.empty()) {
        return;
    }
    const core::Alarm &a = list[std::min(model_.selected(), static_cast<int>(list.size()) - 1)];
    set_text_fmt(hh_, "%02d", a.hour);
    set_text_fmt(mm_, "%02d", a.minute);
    set_color({hh_, colon_, mm_}, a.on ? theme::text() : lv_color_hex(0x4B5563));
    for (int i = 0; i < 7; i++) {
        const bool day = a.on_day((i + 1) % 7);
        set_text_color(days_[i],
                                    day ? (a.on ? theme::accent() : lv_color_hex(0x6B7280)) : theme::rim());
    }
    set_bg_color(badge_, a.on ? lv_color_hex(0x0E3A44) : theme::rim());
    set_text(badge_text_, a.on ? "ON" : "OFF");
    set_text_color(badge_text_, a.on ? theme::accent() : lv_color_hex(0x6B7280));

    std::string info;
    if (!model_.snoozed_until().empty()) {
        info = "snoozed until " + model_.snoozed_until();
    } else if (const std::string next = local ? model_.next(*local) : ""; !next.empty()) {
        info = "next: " + next;
    } else {
        info = local ? "all alarms off" : "clock not set";
    }
    if (list.size() > 1) {
        char n[40];
        std::snprintf(n, sizeof n, "  ·  %d/%d", model_.selected() + 1, static_cast<int>(list.size()));
        info += n;
    }
    set_text(info_, info.c_str());
    set_text_color(info_, model_.snoozed_until().empty() ? theme::muted() : kAmber);
}

} // namespace ui
