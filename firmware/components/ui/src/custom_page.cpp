#include "ui/custom_page.h"

#include <algorithm>
#include <string>

#include "ui/data_pages.h"
#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/round_layout.h"
#include "ui/theme.h"

namespace ui {
namespace {

// Layout on the 412 px design, as in the emulator: content from x 92 to 320, from y 120 on.
constexpr int kLeft = 92, kRight = 320, kTop = 120, kBottom = 352;

lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const std::string &s, int x, int y,
               lv_align_t align = LV_ALIGN_TOP_LEFT)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, s.c_str());
    if (align == LV_ALIGN_TOP_RIGHT) {
        place(l, align, x - kDesignSize, y);
    } else {
        place(l, align, align == LV_ALIGN_TOP_MID ? 0 : x, y);
    }
    return l;
}

std::string with_unit(const core::Value &v, const std::string &unit)
{
    std::string s = format_value(v);
    return unit.empty() ? s : s + " " + unit;
}

} // namespace

std::pair<const char *, lv_color_t> named_icon(std::string_view name)
{
    if (name == "ok" || name == "ci_ok") {
        return {icon::kOk, lv_color_hex(0x4ADE80)};
    }
    if (name == "fail" || name == "ci_fail") {
        return {icon::kError, lv_color_hex(0xF87171)};
    }
    if (name == "warn") {
        return {icon::kWarning, theme::warn()};
    }
    if (name == "running") {
        return {icon::kTimer, theme::accent()};
    }
    if (name == "temp") {
        return {icon::kThermometer, theme::accent()};
    }
    if (name == "drop") {
        return {icon::kHumidity, lv_color_hex(0x7DD3FC)};
    }
    if (name == "rain") {
        return {icon::kRain, lv_color_hex(0x7DD3FC)};
    }
    if (name == "sun") {
        return {icon::kSun, theme::warn()};
    }
    if (name == "server" || name == "vm" || name == "disk") {
        return {icon::kNetwork, theme::text()};
    }
    return {icon::kBell, theme::accent()};
}

CustomPageView *CustomPageView::create(lv_obj_t *parent)
{
    auto *v = new CustomPageView();
    v->root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(v->root_);
    lv_obj_set_size(v->root_, lv_pct(100), lv_pct(100));
    lv_obj_set_clickable(v->root_, false);
    lv_obj_add_event_cb(v->root_, on_delete, LV_EVENT_DELETE, v);
    return v;
}

void CustomPageView::show(const core::CustomPage *page)
{
    lv_obj_clean(root_);
    lines_.clear();
    if (page == nullptr) {
        return;
    }
    std::string title = page->title;
    std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return std::toupper(c); });
    lv_obj_t *t = text(root_, font::small(), theme::muted(), title, 0, 84, LV_ALIGN_TOP_MID);
    lv_obj_set_style_text_letter_space(t, 2, 0);

    int y = kTop;
    for (const core::Widget &w : page->items) {
        if (y > kBottom - 20) {
            break; // what doesn't fit is left out
        }
        if (w.kind == "value") {
            text(root_, font::small(), theme::muted(), w.label, kLeft, y + 3);
            text(root_, font::body(), theme::text(), with_unit(w.value, w.unit), kRight, y, LV_ALIGN_TOP_RIGHT);
            y += 34;
        } else if (w.kind == "bar") {
            text(root_, font::small(), theme::muted(), w.label, kLeft, y);
            text(root_, font::small(), theme::text(), with_unit(w.value, w.unit), kRight, y, LV_ALIGN_TOP_RIGHT);
            const double v = std::holds_alternative<double>(w.value) ? std::get<double>(w.value) : 0;
            lv_color_t c = theme::accent();
            if (w.crit && v >= *w.crit) {
                c = lv_color_hex(0xF87171);
            } else if (w.warn && v >= *w.warn) {
                c = theme::warn();
            }
            lv_obj_t *bar = lv_bar_create(root_);
            lv_obj_set_size(bar, kRight - kLeft, 8);
            place(bar, LV_ALIGN_TOP_LEFT, kLeft, y + 22);
            lv_bar_set_range(bar, 0, 1000);
            lv_bar_set_value(bar, static_cast<int32_t>(std::clamp(v / w.max, 0.0, 1.0) * 1000), LV_ANIM_OFF);
            lv_obj_set_style_bg_color(bar, lv_color_hex(0x2A313B), LV_PART_MAIN);
            lv_obj_set_style_bg_color(bar, c, LV_PART_INDICATOR);
            lv_obj_set_clickable(bar, false);
            y += 40;
        } else if (w.kind == "icon+text") {
            const auto [sym, color] = named_icon(w.icon);
            text(root_, font::icons(), color, sym, kLeft, y);
            text(root_, font::body(), theme::text(), w.text, kLeft + 34, y);
            y += 34;
        } else if (w.kind == "list") {
            if (!w.label.empty()) {
                text(root_, font::small(), theme::muted(), w.label, kLeft, y);
                y += 24;
            }
            for (const std::string &l : w.lines) {
                if (y > kBottom - 20) {
                    break;
                }
                text(root_, font::small(), theme::text(), l, kLeft + 8, y);
                y += 22;
            }
        } else if (w.kind == "sparkline") {
            text(root_, font::small(), theme::muted(), w.label, kLeft, y);
            if (w.values.size() > 1) {
                const auto [lo_it, hi_it] = std::minmax_element(w.values.begin(), w.values.end());
                const double lo = *lo_it, span = *hi_it - *lo_it > 0 ? *hi_it - *lo_it : 1;
                auto &pts = lines_.emplace_back(w.values.size());
                for (size_t i = 0; i < w.values.size(); i++) {
                    pts[i].x = static_cast<lv_value_precise_t>((kRight - kLeft) * static_cast<double>(i) / (w.values.size() - 1));
                    pts[i].y = static_cast<lv_value_precise_t>(32 - 32 * (w.values[i] - lo) / span);
                }
                lv_obj_t *box = lv_obj_create(root_);
                lv_obj_remove_style_all(box);
                lv_obj_set_size(box, kRight - kLeft, 34);
                place(box, LV_ALIGN_TOP_LEFT, kLeft, y + 20);
                lv_obj_set_clickable(box, false);
                lv_obj_t *line = lv_line_create(box);
                lv_obj_set_style_line_width(line, 2, 0);
                lv_obj_set_style_line_color(line, theme::accent(), 0);
                lv_obj_set_style_line_rounded(line, true, 0);
                lv_line_set_points(line, pts.data(), static_cast<uint32_t>(pts.size()));
            }
            y += 60;
        }
    }
}

void CustomPageView::on_delete(lv_event_t *e)
{
    delete static_cast<CustomPageView *>(lv_event_get_user_data(e));
}

} // namespace ui
