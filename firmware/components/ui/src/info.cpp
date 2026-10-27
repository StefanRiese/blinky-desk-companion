#include "ui/info.h"

#include <cstring>
#include <string>

#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/round_layout.h"
#include "ui/theme.h"

namespace ui {
namespace {

const lv_color_t kOk = lv_color_hex(0x4ADE80);
const lv_color_t kError = lv_color_hex(0xF87171);
constexpr int kRowWidth = 290; // fits the circle down to the last source row

lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

void set_text(lv_obj_t *l, const std::string &s)
{
    if (s != lv_label_get_text(l)) {
        lv_label_set_text(l, s.c_str());
    }
}

// row is an icon (or a status dot, if icon_font is null) and one line of text.
template <typename Row> Row make_row(lv_obj_t *parent, int y, const lv_font_t *icon_font)
{
    Row r;
    r.box = lv_obj_create(parent);
    lv_obj_remove_style_all(r.box);
    lv_obj_set_size(r.box, kRowWidth, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r.box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r.box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r.box, 8, 0);
    lv_obj_set_clickable(r.box, false);
    place(r.box, LV_ALIGN_TOP_MID, 0, y);
    if (icon_font != nullptr) {
        r.icon = make_label(r.box, icon_font, theme::muted());
    } else {
        r.icon = lv_obj_create(r.box);
        lv_obj_remove_style_all(r.icon);
        lv_obj_set_size(r.icon, 8, 8);
        lv_obj_set_style_radius(r.icon, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(r.icon, LV_OPA_COVER, 0);
        lv_obj_set_style_margin_left(r.icon, 8, 0);
        lv_obj_set_style_margin_right(r.icon, 8, 0);
    }
    r.text = make_label(r.box, font::small(), theme::text());
    lv_obj_set_flex_grow(r.text, 1);
    lv_obj_set_height(r.text, lv_font_get_line_height(font::small())); // one line, cut with "…"
    lv_label_set_long_mode(r.text, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(r.text, "");
    return r;
}

void set_link(lv_obj_t *icon, lv_obj_t *text, const char *sym, lv_color_t color, const std::string &s)
{
    if (std::strcmp(lv_label_get_text(icon), sym) != 0) {
        lv_label_set_text(icon, sym);
    }
    lv_obj_set_style_text_color(icon, color, 0);
    set_text(text, s);
}

} // namespace

InfoPage *InfoPage::create(lv_obj_t *parent, const InfoContext &ctx)
{
    auto *p = new InfoPage(ctx);
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_clickable(page, false);
    lv_obj_add_event_cb(page, on_delete, LV_EVENT_DELETE, p);

    // Ring along the edge: shows whether the round screen is aligned and fully visible.
    lv_obj_t *rim = lv_arc_create(page);
    lv_obj_set_size(rim, lv_pct(100), lv_pct(100));
    lv_obj_center(rim);
    lv_arc_set_bg_angles(rim, 0, 360);
    lv_arc_set_value(rim, 0);
    lv_obj_set_style_arc_color(rim, theme::rim(), LV_PART_MAIN);
    lv_obj_set_style_arc_width(rim, theme::kRimWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(rim, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(rim, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_clickable(rim, false);

    lv_obj_t *title = make_label(page, font::title(), theme::text());
    lv_label_set_text(title, "Blinky");
    place(title, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_t *about = make_label(page, font::small(), theme::muted());
    lv_label_set_text_fmt(about, "fw %s  ·  %s  ·  LVGL %d.%d", ctx.version, ctx.board, LVGL_VERSION_MAJOR,
                          LVGL_VERSION_MINOR);
    place(about, LV_ALIGN_TOP_MID, 0, 82);

    p->wifi_ = make_row<Row>(page, 112, font::icons());
    p->usb_ = make_row<Row>(page, 142, font::icons());
    p->tcp_ = make_row<Row>(page, 172, font::icons());
    for (int i = 0; i < kMaxSources; i++) {
        p->sources_[i] = make_row<Row>(page, 214 + i * 24, nullptr);
    }

    p->touch_label_ = make_label(page, font::small(), theme::warn());
    lv_label_set_text(p->touch_label_, "touch the screen");
    place(p->touch_label_, LV_ALIGN_TOP_MID, 0, 346);

    p->dot_ = lv_obj_create(page);
    lv_obj_remove_style_all(p->dot_);
    lv_obj_set_size(p->dot_, 16, 16);
    lv_obj_set_style_radius(p->dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(p->dot_, theme::warn(), 0);
    lv_obj_set_style_bg_opa(p->dot_, LV_OPA_COVER, 0);
    lv_obj_set_hidden(p->dot_, true);
    lv_obj_set_clickable(p->dot_, false);

    lv_obj_add_event_cb(parent, on_touch, LV_EVENT_PRESSING, p);
    lv_obj_add_event_cb(parent, on_touch, LV_EVENT_CLICKED, p);
    p->refresh();
    p->timer_ = lv_timer_create(on_timer, 1000, p);
    return p;
}

void InfoPage::refresh()
{
    const core::Status st = ctx_.status ? ctx_.status() : core::Status{};

    using Wifi = core::Status::Wifi;
    const char *wifi_icon = st.wifi == Wifi::Off || st.wifi == Wifi::Connecting ? icon::kWifiOff : icon::kWifi;
    lv_color_t wifi_color = theme::muted();
    if (st.wifi == Wifi::Online) {
        wifi_color = kOk;
    } else if (st.wifi == Wifi::NoInternet || st.wifi == Wifi::SetupAp) {
        wifi_color = theme::warn();
    }
    set_link(wifi_.icon, wifi_.text, wifi_icon, wifi_color, st.wifi_text.empty() ? "WiFi not set up" : st.wifi_text);
    set_link(usb_.icon, usb_.text, icon::kUsb, st.usb.up ? kOk : theme::muted(),
             st.usb.text.empty() ? "no host on USB" : st.usb.text);
    set_link(tcp_.icon, tcp_.text, icon::kNetwork, st.tcp.up ? kOk : theme::muted(),
             st.tcp.text.empty() ? "no TCP host set" : st.tcp.text);

    // Sources: "weather  usb", "aqi  WiFi has no internet, …"; the last row sums up the rest.
    const size_t n = st.sources.size();
    for (size_t i = 0; i < kMaxSources; i++) {
        Row &r = sources_[i];
        lv_obj_set_hidden(r.box, i >= n);
        if (i >= n) {
            continue;
        }
        if (i == kMaxSources - 1 && n > kMaxSources) {
            lv_obj_set_style_bg_color(r.icon, theme::muted(), 0);
            set_text(r.text, "+ " + std::to_string(n - i) + " more sources");
            continue;
        }
        const auto &s = st.sources[i];
        lv_color_t color = theme::muted();
        if (s.state == "ok") {
            color = kOk;
        } else if (s.state == "error" || s.state == "backoff") {
            color = kError;
        } else if (s.state == "unavailable") {
            color = theme::warn();
        }
        lv_obj_set_style_bg_color(r.icon, color, 0);
        std::string line = s.sid + "  ·  ";
        if (!s.msg.empty() && s.state != "ok") {
            line += s.msg;
        } else {
            line += s.via == "host" ? s.link : s.via;
        }
        set_text(r.text, line);
    }
}

void InfoPage::on_touch(lv_event_t *e)
{
    auto *p = static_cast<InfoPage *>(lv_event_get_user_data(e));
    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_obj_set_hidden(p->dot_, false);
    lv_obj_set_pos(p->dot_, pt.x - 8, pt.y - 8);
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        p->taps_++;
    }
    lv_label_set_text_fmt(p->touch_label_, "touch %d, %d  ·  taps %d", static_cast<int>(pt.x),
                          static_cast<int>(pt.y), p->taps_);
}

void InfoPage::on_timer(lv_timer_t *t)
{
    static_cast<InfoPage *>(lv_timer_get_user_data(t))->refresh();
}

void InfoPage::on_delete(lv_event_t *e)
{
    auto *p = static_cast<InfoPage *>(lv_event_get_user_data(e));
    lv_timer_delete(p->timer_);
    delete p;
}

} // namespace ui
