#include "ui/data_pages.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app_core/units.h"
#include "ui/dashboard.h"
#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/round_layout.h"
#include "ui/theme.h"
#include "set.h"

namespace ui {
namespace {

constexpr const char *kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const lv_color_t kRainBlue = lv_color_hex(0x7DD3FC);

lv_obj_t *page(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_clickable(p, false);
    return p;
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    set_text_color(l, color);
    set_text(l, "");
    return l;
}

void title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *t = label(parent, font::small(), theme::muted());
    lv_obj_set_style_text_letter_space(t, 2, 0);
    set_text(t, text);
    place(t, LV_ALIGN_TOP_MID, 0, 84);
}

// set changes a label only when its text or colour differs, and shows or hides it.
void set(lv_obj_t *l, bool shown, const char *text = "", lv_color_t color = theme::text())
{
    lv_obj_set_hidden(l, !shown);
    if (shown && std::strcmp(lv_label_get_text(l), text) != 0) {
        set_text(l, text);
    }
    if (shown) {
        set_text_color(l, color);
    }
}

// weekday returns the day of the week of a date "YYYY-MM-DD" (0 = Sunday), or -1.
int weekday(const std::string &d)
{
    int y = 0, m = 0, day = 0;
    if (std::sscanf(d.c_str(), "%d-%d-%d", &y, &m, &day) != 3 || m < 1 || m > 12) {
        return -1;
    }
    static constexpr int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) {
        y--;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + day) % 7;
}

core::Units units(const core::Settings &s)
{
    return {s.get("units.temp"), s.get("units.wind"), s.get("units.rain")};
}

} // namespace

std::string format_value(const core::Value &v, size_t max)
{
    if (const auto *b = std::get_if<bool>(&v)) {
        return *b ? "yes" : "no";
    }
    if (const auto *d = std::get_if<double>(&v)) {
        char buf[32];
        if (*d == std::floor(*d) && std::fabs(*d) < 1e12) {
            std::snprintf(buf, sizeof buf, "%.0f", *d);
        } else {
            std::snprintf(buf, sizeof buf, "%.2f", *d);
            std::string s = buf; // 41.50 → 41.5
            while (s.back() == '0') {
                s.pop_back();
            }
            return s;
        }
        return buf;
    }
    if (const auto *s = std::get_if<std::string>(&v)) {
        if (s->size() <= max) {
            return *s;
        }
        size_t cut = max - 1;
        while (cut > 0 && (static_cast<unsigned char>((*s)[cut]) & 0xC0) == 0x80) {
            cut--; // don't split a UTF-8 character
        }
        return s->substr(0, cut) + "…";
    }
    return "";
}

// ---- forecast ----

ForecastPage *ForecastPage::create(lv_obj_t *parent, const DataPageContext &ctx)
{
    auto *f = new ForecastPage(ctx);
    lv_obj_t *p = page(parent);
    lv_obj_add_event_cb(p, on_delete, LV_EVENT_DELETE, f);
    title(p, "FORECAST");
    for (int i = 0; i < kDays; i++) {
        const int y = 120 + i * 76;
        Row &r = f->rows_[i];
        r.name = label(p, font::body(), theme::text());
        place(r.name, LV_ALIGN_TOP_LEFT, 64, y + 14);
        r.icon = label(p, font::icons_large(), theme::text());
        place(r.icon, LV_ALIGN_TOP_LEFT, 132, y);
        r.temps = label(p, font::body(), theme::text());
        place(r.temps, LV_ALIGN_TOP_LEFT, 200, y + 2);
        r.detail = label(p, font::small(), theme::muted());
        place(r.detail, LV_ALIGN_TOP_LEFT, 200, y + 30);
    }
    f->none_ = label(p, font::small(), theme::muted());
    set_text(f->none_, "no forecast");
    place(f->none_, LV_ALIGN_TOP_MID, 0, 196);
    f->refresh();
    f->timer_ = lv_timer_create(on_timer, 1000, f);
    return f;
}

void ForecastPage::refresh()
{
    const core::DataStore &s = *ctx_.store;
    const core::Units u = units(*ctx_.settings);
    bool any = false;
    for (int i = 0; i < kDays; i++) {
        const std::string p = "weather.day" + std::to_string(i) + ".";
        const auto max = s.get(p + "max"), min = s.get(p + "min"), code = s.get(p + "code");
        const bool have = max && min && code && std::holds_alternative<double>(max->value) &&
                          std::holds_alternative<double>(min->value) && std::holds_alternative<double>(code->value);
        Row &r = rows_[i];
        if (!have) {
            for (lv_obj_t *l : {r.name, r.icon, r.temps, r.detail}) {
                set(l, false);
            }
            continue;
        }
        any = true;
        const lv_color_t c = max->stale ? theme::muted() : theme::text();
        const auto d = s.get(p + "d");
        const int wd = d && std::holds_alternative<std::string>(d->value) ? weekday(std::get<std::string>(d->value)) : -1;
        set(r.name, true, i == 0 ? "Today" : wd >= 0 ? kWeekdays[wd] : "–", c);
        set(r.icon, true, weather_icon(static_cast<int>(std::get<double>(code->value)), true), c);
        const std::string temps = u.temperature(std::get<double>(max->value), 0, false) + " / " +
                                  u.temperature(std::get<double>(min->value), 0, false);
        set(r.temps, true, temps.c_str(), c);
        std::string detail;
        if (const auto pop = s.number(p + "pop")) {
            detail = std::to_string(static_cast<int>(*pop)) + " % rain";
        }
        const auto rise = s.text(p + "sunrise"), down = s.text(p + "sunset");
        if (rise && down && !rise->empty()) {
            detail += (detail.empty() ? "" : "  ·  ") + *rise + "–" + *down;
        }
        set(r.detail, !detail.empty(), detail.c_str(), max->stale ? theme::muted() : kRainBlue);
    }
    set(none_, !any, "no forecast", theme::muted());
}

void ForecastPage::on_timer(lv_timer_t *t)
{
    static_cast<ForecastPage *>(lv_timer_get_user_data(t))->refresh();
}

void ForecastPage::on_delete(lv_event_t *e)
{
    auto *f = static_cast<ForecastPage *>(lv_event_get_user_data(e));
    lv_timer_delete(f->timer_);
    delete f;
}

// ---- indoor ----

namespace {
constexpr int kChartX0 = 96, kChartX1 = 316, kChartY0 = 262, kChartY1 = 322; // on the 412 px design
} // namespace

IndoorPage *IndoorPage::create(lv_obj_t *parent, const DataPageContext &ctx)
{
    auto *in = new IndoorPage(ctx);
    lv_obj_t *p = page(parent);
    lv_obj_add_event_cb(p, on_delete, LV_EVENT_DELETE, in);
    title(p, "INDOOR");
    in->temp_ = label(p, font::digits(), theme::text());
    place(in->temp_, LV_ALIGN_TOP_MID, 0, 112);
    in->hum_ = label(p, font::body(), kRainBlue);
    place(in->hum_, LV_ALIGN_TOP_MID, 0, 200);
    in->none_ = label(p, font::small(), theme::muted());
    place(in->none_, LV_ALIGN_TOP_MID, 0, 196);

    // The trend: a line in a box at the chart's place, the range below.
    lv_obj_t *box = lv_obj_create(p);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, kChartX1 - kChartX0, kChartY1 - kChartY0);
    place(box, LV_ALIGN_TOP_LEFT, kChartX0, kChartY0);
    lv_obj_set_clickable(box, false);
    in->line_ = lv_line_create(box);
    lv_obj_set_style_line_width(in->line_, 2, 0);
    lv_obj_set_style_line_color(in->line_, theme::accent(), 0);
    lv_obj_set_style_line_rounded(in->line_, true, 0);
    lv_obj_set_clickable(in->line_, false);
    in->range_ = label(p, font::small(), theme::muted());
    place(in->range_, LV_ALIGN_TOP_MID, 0, 330);
    in->noise_ = label(p, font::small(), theme::muted());
    place(in->noise_, LV_ALIGN_TOP_MID, 0, 352);

    in->sample();
    in->refresh();
    in->timer_ = lv_timer_create(on_timer, 1000, in);
    in->sampler_ = lv_timer_create(on_sample, kSampleMs, in);
    return in;
}

void IndoorPage::sample()
{
    if (const auto t = ctx_.store->number("indoor.temp")) {
        hist_.push_back(*t);
        if (hist_.size() > kSamples) {
            hist_.pop_front();
        }
    }
}

void IndoorPage::refresh()
{
    const core::DataStore &s = *ctx_.store;
    const core::Units u = units(*ctx_.settings);
    const auto t = s.number("indoor.temp");
    set(temp_, t.has_value(), t ? u.temperature(*t, 1, false).c_str() : "");
    const auto h = s.number("indoor.hum");
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d %% humidity", h ? static_cast<int>(std::lround(*h)) : 0);
    set(hum_, h.has_value(), buf, kRainBlue);
    set(none_, !t, "no indoor sensor", theme::muted());

    const auto noise = s.number("indoor.noise");
    std::snprintf(buf, sizeof buf, "noise %d dB", noise ? static_cast<int>(*noise) : 0);
    set(noise_, noise.has_value(), buf, noise && *noise >= 70 ? lv_color_hex(0xF87171) : theme::muted());

    if (hist_.size() < 2) {
        lv_obj_set_hidden(line_, true);
        set(range_, false);
        return;
    }
    const auto [lo_it, hi_it] = std::minmax_element(hist_.begin(), hist_.end());
    const double lo = *lo_it - 0.2, hi = *hi_it + 0.2;
    const size_t n = hist_.size();
    for (size_t i = 0; i < n; i++) {
        points_[i].x = static_cast<lv_value_precise_t>((kChartX1 - kChartX0) * static_cast<double>(i) / (n - 1));
        points_[i].y = static_cast<lv_value_precise_t>((kChartY1 - kChartY0) * (hi - hist_[i]) / (hi - lo));
    }
    lv_line_set_points(line_, points_.data(), static_cast<uint32_t>(n));
    lv_obj_set_hidden(line_, false);
    const std::string range = u.temperature(lo + 0.2, 1, false) + "–" + u.temperature(hi - 0.2, 1, true) + "  ·  last " +
                              std::to_string((n * kSampleMs + 30'000) / 60'000) + " min";
    set(range_, true, range.c_str(), theme::muted());
}

void IndoorPage::on_timer(lv_timer_t *t)
{
    static_cast<IndoorPage *>(lv_timer_get_user_data(t))->refresh();
}

void IndoorPage::on_sample(lv_timer_t *t)
{
    static_cast<IndoorPage *>(lv_timer_get_user_data(t))->sample();
}

void IndoorPage::on_delete(lv_event_t *e)
{
    auto *in = static_cast<IndoorPage *>(lv_event_get_user_data(e));
    lv_timer_delete(in->timer_);
    lv_timer_delete(in->sampler_);
    delete in;
}

// ---- host data ----

HostDataPage *HostDataPage::create(lv_obj_t *parent, const DataPageContext &ctx)
{
    auto *h = new HostDataPage(ctx);
    lv_obj_t *p = page(parent);
    lv_obj_add_event_cb(p, on_delete, LV_EVENT_DELETE, h);
    title(p, "HOST DATA");
    for (int i = 0; i < kRows; i++) {
        const int y = 116 + i * 30;
        h->rows_[i][0] = label(p, font::small(), theme::muted());
        place(h->rows_[i][0], LV_ALIGN_TOP_LEFT, 74, y + 3);
        h->rows_[i][1] = label(p, font::body(), theme::text());
        place(h->rows_[i][1], LV_ALIGN_TOP_RIGHT, -74, y);
    }
    h->none_ = label(p, font::small(), theme::muted());
    set_text(h->none_, "no host data");
    place(h->none_, LV_ALIGN_TOP_MID, 0, 186);
    h->hint_ = label(p, font::small(), theme::muted());
    set_text(h->hint_, "add sources such as sys or http in the settings");
    place(h->hint_, LV_ALIGN_TOP_MID, 0, 210);
    h->refresh();
    h->timer_ = lv_timer_create(on_timer, 1000, h);
    return h;
}

void HostDataPage::refresh()
{
    std::vector<std::pair<std::string, core::Item>> rows;
    for (auto &kv : ctx_.store->items()) {
        if (kv.first.rfind("weather.", 0) != 0 && kv.first.rfind("indoor.", 0) != 0) {
            rows.push_back(std::move(kv));
        }
    }
    for (int i = 0; i < kRows; i++) {
        const bool shown = i < static_cast<int>(rows.size());
        if (!shown) {
            set(rows_[i][0], false);
            set(rows_[i][1], false);
            continue;
        }
        const auto &[key, item] = rows[i];
        const std::string k = format_value(key, 22), v = format_value(item.value);
        set(rows_[i][0], true, k.c_str(), item.stale ? lv_color_hex(0x4B5563) : theme::muted());
        set(rows_[i][1], true, v.c_str(), item.stale ? lv_color_hex(0x4B5563) : theme::text());
    }
    set(none_, rows.empty(), "no host data", theme::muted());
    set(hint_, rows.empty(), "add sources such as sys or http in the settings", theme::muted());
}

void HostDataPage::on_timer(lv_timer_t *t)
{
    static_cast<HostDataPage *>(lv_timer_get_user_data(t))->refresh();
}

void HostDataPage::on_delete(lv_event_t *e)
{
    auto *h = static_cast<HostDataPage *>(lv_event_get_user_data(e));
    lv_timer_delete(h->timer_);
    delete h;
}

} // namespace ui
