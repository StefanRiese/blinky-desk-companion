// The eyes redraw only what changed, in strips of 40 lines like the boards: after running
// the animation for a while, the screen must look the same as a full redraw. Catches areas
// that are invalidated too small, skipped frames that did change, and leftovers of extras.
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "headless.h"
#include "png.h"
#include "ui/eyes.h"
#include "ui/theme.h"

namespace {

constexpr int kSize = 412, kLines = 40;

struct StripScreen {
    std::vector<uint32_t> strip = std::vector<uint32_t>(kSize * kLines);
    std::vector<uint32_t> frame = std::vector<uint32_t>(kSize * kSize);
    lv_display_t *disp = nullptr;

    StripScreen()
    {
        sim::init();
        disp = lv_display_create(kSize, kSize);
        lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
        lv_display_set_buffers(disp, strip.data(), nullptr, static_cast<uint32_t>(strip.size() * 4),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_user_data(disp, this);
        lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *a, uint8_t *px) {
            auto *self = static_cast<StripScreen *>(lv_display_get_user_data(d));
            const auto *src = reinterpret_cast<const uint32_t *>(px);
            const int32_t w = lv_area_get_width(a);
            for (int32_t y = a->y1; y <= a->y2; y++) {
                for (int32_t x = a->x1; x <= a->x2; x++) {
                    self->frame[y * kSize + x] = src[(y - a->y1) * w + (x - a->x1)] & 0xFFFFFF;
                }
            }
            lv_display_flush_ready(d);
        });
    }
    ~StripScreen() { lv_display_delete(disp); }

    // run lets the animation and the partial redraws run for ms (real time: LVGL's tick).
    static void run(int ms)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            lv_timer_handler();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }

    // differing counts the pixels that change when everything is drawn again.
    int differing()
    {
        lv_refr_now(disp); // what the last animation step invalidated
        const std::vector<uint32_t> partial = frame;
        lv_obj_invalidate(lv_display_get_screen_active(disp));
        lv_refr_now(disp);
        int n = 0, x1 = kSize, y1 = kSize, x2 = -1, y2 = -1;
        for (size_t i = 0; i < frame.size(); i++) {
            if (partial[i] != frame[i]) {
                n++;
                const int x = static_cast<int>(i % kSize), y = static_cast<int>(i / kSize);
                x1 = std::min(x1, x), y1 = std::min(y1, y), x2 = std::max(x2, x), y2 = std::max(y2, y);
            }
        }
        if (n > 0 && std::getenv("DUMP_EYES") != nullptr) {
            auto rgb = [](const std::vector<uint32_t> &f) {
                std::vector<uint8_t> o;
                for (uint32_t p : f) {
                    o.push_back(static_cast<uint8_t>(p >> 16));
                    o.push_back(static_cast<uint8_t>(p >> 8));
                    o.push_back(static_cast<uint8_t>(p));
                }
                return o;
            };
            png::write_rgb(std::string(std::getenv("DUMP_EYES")) + "/partial.png", kSize, kSize, rgb(partial));
            png::write_rgb(std::string(std::getenv("DUMP_EYES")) + "/full.png", kSize, kSize, rgb(frame));
        }
        if (n > 0) {
            MESSAGE("differ in x ", x1 - 206, "..", x2 - 206, ", y ", y1 - 191, "..", y2 - 191, " from the eyes' centre");
        }
        return n;
    }
};

} // namespace

TEST_CASE("eyes: partial redraws in strips match a full redraw")
{
    lv_display_t *before = lv_display_get_default();
    StripScreen s;
    lv_display_set_default(s.disp);
    lv_obj_t *scr = lv_display_get_screen_active(s.disp);
    lv_obj_set_style_bg_color(scr, ui::theme::background(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    ui::Eyes *eyes = ui::Eyes::create(scr, 7);
    lv_refr_now(s.disp);

    // Moods with extras (moving and still), lids, cheeks, a circling gaze; each after another,
    // so the old mood's extras must be cleared too.
    for (const char *mood : {"hot", "happy", "sleepy", "love", "sad", "dizzy", "neutral"}) {
        const std::string name = mood;
        CAPTURE(name);
        eyes->set_mood(mood);
        StripScreen::run(700);
        CHECK(s.differing() == 0);
    }
    lv_obj_delete(eyes->obj());
    lv_display_set_default(before);
}
