#include "ui/dashboard.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "app_core/units.h"
#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/round_layout.h"
#include "ui/theme.h"

namespace ui {
namespace {

constexpr const char *kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
constexpr const char *kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t *row(lv_obj_t *parent, int y, int gap)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, gap, 0);
    lv_obj_set_clickable(r, false);
    place(r, LV_ALIGN_TOP_MID, 0, y);
    return r;
}

} // namespace

const char *weather_icon(int c, bool day)
{
    if (c <= 2 && !day) {
        return icon::kMoon;
    }
    if (c == 0) {
        return icon::kSun;
    }
    if (c <= 2) {
        return icon::kPartlyCloudy;
    }
    if (c == 3) {
        return icon::kCloud;
    }
    if (c == 45 || c == 48) {
        return icon::kFog;
    }
    if ((c >= 71 && c <= 77) || c == 85 || c == 86) {
        return icon::kSnow;
    }
    if (c >= 95) {
        return icon::kThunder;
    }
    if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) {
        return icon::kRain;
    }
    return icon::kCloud;
}

// Same names as the emulator's WMO table.
const char *weather_label(int c)
{
    if (c == 0) {
        return "Clear";
    }
    if (c <= 2) {
        return "Partly cloudy";
    }
    if (c == 3) {
        return "Overcast";
    }
    if (c == 45 || c == 48) {
        return "Fog";
    }
    if (c >= 51 && c <= 57) {
        return "Drizzle";
    }
    if (c >= 61 && c <= 67) {
        return "Rain";
    }
    if ((c >= 71 && c <= 77) || c == 85 || c == 86) {
        return "Snow";
    }
    if (c >= 80 && c <= 82) {
        return "Showers";
    }
    if (c >= 95) {
        return "Thunderstorm";
    }
    return "Unknown";
}

Dashboard *Dashboard::create(lv_obj_t *parent, const DashboardContext &ctx)
{
    auto *d = new Dashboard(ctx);
    for (Text *t : d->texts()) {
        t->sub = lv_subject_create(LV_SUBJECT_TYPE_STRING);
        lv_subject_set_string_buffer_static(t->sub, t->buf, t->prev, sizeof t->buf);
    }

    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_clickable(page, false);
    lv_obj_add_event_cb(page, on_delete, LV_EVENT_DELETE, d);

    // Clock and date
    lv_obj_t *clock = label(page, font::digits(), theme::text());
    place(clock, LV_ALIGN_TOP_MID, 0, 92);
    lv_label_bind_text(clock, d->clock_.sub, nullptr);
    lv_obj_t *date = label(page, font::small(), theme::muted());
    place(date, LV_ALIGN_TOP_MID, 0, 176);
    lv_label_bind_text(date, d->date_.sub, nullptr);

    // Weather: icon, temperature, then description · humidity · wind
    d->weather_row_ = row(page, 206, 14);
    lv_label_bind_text(label(d->weather_row_, font::icons_large(), theme::text()), d->w_icon_.sub, nullptr);
    lv_obj_t *col = lv_obj_create(d->weather_row_);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_label_bind_text(label(col, font::large(), theme::text()), d->w_temp_.sub, nullptr);
    lv_label_bind_text(label(col, font::small(), theme::muted()), d->w_line_.sub, nullptr);
    d->no_weather_ = label(page, font::small(), theme::muted());
    lv_label_set_text(d->no_weather_, "no weather data");
    place(d->no_weather_, LV_ALIGN_TOP_MID, 0, 240);

    // Indoor: home icon, temperature, drop icon, humidity
    d->indoor_row_ = row(page, 296, 6);
    lv_label_set_text(label(d->indoor_row_, font::icons(), theme::muted()), icon::kHome);
    lv_label_bind_text(label(d->indoor_row_, font::body(), theme::text()), d->in_temp_.sub, nullptr);
    lv_obj_t *drop = label(d->indoor_row_, font::icons(), theme::muted());
    lv_label_set_text(drop, icon::kHumidity);
    lv_obj_set_style_margin_left(drop, 12, 0);
    lv_label_bind_text(label(d->indoor_row_, font::body(), theme::text()), d->in_hum_.sub, nullptr);

    // Rain in the next two hours (only when some is expected)
    d->rain_row_ = row(page, 332, 6);
    lv_label_set_text(label(d->rain_row_, font::icons(), lv_color_hex(0x7DD3FC)), icon::kRain);
    lv_label_bind_text(label(d->rain_row_, font::small(), lv_color_hex(0x7DD3FC)), d->rain_.sub, nullptr);

    d->refresh();
    d->timer_ = lv_timer_create(on_timer, 1000, d);
    return d;
}

void Dashboard::refresh()
{
    const core::DataStore &s = *ctx_.store;
    const core::Settings &cfg = *ctx_.settings;
    const core::Units u{cfg.get("units.temp"), cfg.get("units.wind"), cfg.get("units.rain")};
    char buf[64];

    std::tm t{};
    if (ctx_.local_time && ctx_.local_time(t)) {
        std::snprintf(buf, sizeof buf, "%02d:%02d", t.tm_hour, t.tm_min);
        lv_subject_set_string(clock_.sub, buf);
        std::snprintf(buf, sizeof buf, "%s, %d %s", kDays[t.tm_wday % 7], t.tm_mday, kMonths[t.tm_mon % 12]);
        lv_subject_set_string(date_.sub, buf);
    } else {
        lv_subject_set_string(clock_.sub, "--:--");
        lv_subject_set_string(date_.sub, "time not set");
    }

    const auto temp = s.number("weather.now.temp");
    lv_obj_set_hidden(weather_row_, !temp.has_value());
    lv_obj_set_hidden(no_weather_, temp.has_value());
    if (temp) {
        const int code = static_cast<int>(s.number("weather.now.code").value_or(-1));
        const bool day = s.number("weather.now.is_day").value_or(1) != 0;
        lv_subject_set_string(w_icon_.sub, weather_icon(code, day));
        lv_subject_set_string(w_temp_.sub, u.temperature(*temp, 1, false).c_str());
        std::string line = weather_label(code);
        if (const auto hum = s.number("weather.now.hum")) {
            std::snprintf(buf, sizeof buf, "  ·  %.0f %%", *hum);
            line += buf;
        }
        if (const auto wind = s.number("weather.now.wind")) {
            line += "  ·  " + u.wind_speed(*wind);
        }
        lv_subject_set_string(w_line_.sub, line.c_str());
    }

    const auto in_t = s.number("indoor.temp"), in_h = s.number("indoor.hum");
    lv_obj_set_hidden(indoor_row_, !in_t && !in_h);
    lv_subject_set_string(in_temp_.sub, in_t ? u.temperature(*in_t, 1, false).c_str() : "--");
    std::snprintf(buf, sizeof buf, in_h ? "%.0f %%" : "--", in_h.value_or(0));
    lv_subject_set_string(in_hum_.sub, buf);

    const auto rain = s.number("weather.rain_next_2h");
    lv_obj_set_hidden(rain_row_, !rain || *rain <= 0);
    if (rain && *rain > 0) {
        lv_subject_set_string(rain_.sub, (u.rain_amount(*rain) + " in the next 2 h").c_str());
    }
}

void Dashboard::on_timer(lv_timer_t *t)
{
    static_cast<Dashboard *>(lv_timer_get_user_data(t))->refresh();
}

void Dashboard::on_delete(lv_event_t *e)
{
    auto *d = static_cast<Dashboard *>(lv_event_get_user_data(e));
    lv_timer_delete(d->timer_);
    for (Text *t : d->texts()) {
        lv_subject_delete(t->sub);
    }
    delete d;
}

} // namespace ui
