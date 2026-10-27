#include "ui/status_bar.h"

#include <cstdio>
#include <cstring>

#include "app_core/units.h"
#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/theme.h"

namespace ui {
namespace {

lv_obj_t *item(lv_obj_t *parent, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, theme::muted(), 0);
    lv_label_set_text(l, "");
    return l;
}

// set changes a label only when its text or colour differs (no redraw otherwise).
void set(lv_obj_t *l, bool shown, const char *text = "", lv_color_t color = theme::muted())
{
    lv_obj_set_hidden(l, !shown);
    if (!shown) {
        return;
    }
    if (std::strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
    lv_obj_set_style_text_color(l, color, 0);
}

} // namespace

StatusBar *StatusBar::create(lv_obj_t *parent, const StatusContext &ctx, bool icons_only)
{
    auto *b = new StatusBar(ctx, icons_only);
    b->row_ = lv_obj_create(parent);
    lv_obj_remove_style_all(b->row_);
    lv_obj_set_size(b->row_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(b->row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b->row_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b->row_, 6, 0);
    lv_obj_set_clickable(b->row_, false);
    lv_obj_align(b->row_, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_add_event_cb(b->row_, on_delete, LV_EVENT_DELETE, b);

    b->time_ = item(b->row_, font::small());
    b->temp_ = item(b->row_, font::small());
    b->stale_ = item(b->row_, font::icons());
    b->wifi_ = item(b->row_, font::icons());
    b->usb_ = item(b->row_, font::icons());
    b->tcp_ = item(b->row_, font::icons());
    b->refresh();
    return b;
}

void StatusBar::refresh()
{
    char buf[32] = "";
    std::tm t{};
    const bool have_time = !icons_only_ && ctx_.local_time && ctx_.local_time(t);
    if (have_time) {
        std::snprintf(buf, sizeof buf, "%02d:%02d", t.tm_hour, t.tm_min);
    }
    set(time_, have_time, buf, theme::text());

    // Outdoor temperature; greyed out with a warning sign once it is stale.
    const auto w = ctx_.store ? ctx_.store->get("weather.now.temp") : std::nullopt;
    const bool have_temp = !icons_only_ && w && std::holds_alternative<double>(w->value);
    if (have_temp) {
        const core::Units u{ctx_.settings->get("units.temp"), "kmh", "mm"};
        std::snprintf(buf, sizeof buf, "%s", u.temperature(std::get<double>(w->value), 0, true).c_str());
    }
    set(temp_, have_temp, buf, have_temp && w->stale ? theme::muted() : theme::text());
    set(stale_, have_temp && w->stale, icon::kWarning, theme::warn());

    const core::Status st = ctx_.status ? ctx_.status() : core::Status{};
    using Wifi = core::Status::Wifi;
    switch (st.wifi) {
    case Wifi::Off:
        set(wifi_, false);
        break;
    case Wifi::Online:
        set(wifi_, true, icon::kWifi, theme::text());
        break;
    case Wifi::NoInternet:
    case Wifi::SetupAp:
        set(wifi_, true, icon::kWifi, theme::warn());
        break;
    case Wifi::Connecting:
        set(wifi_, true, icon::kWifiOff, theme::muted());
        break;
    }
    set(usb_, st.usb.up, icon::kUsb, theme::text());
    set(tcp_, st.tcp.up, icon::kNetwork, theme::text());
}

void StatusBar::on_delete(lv_event_t *e)
{
    delete static_cast<StatusBar *>(lv_event_get_user_data(e));
}

} // namespace ui
