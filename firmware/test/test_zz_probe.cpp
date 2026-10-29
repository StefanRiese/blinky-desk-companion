#include <doctest/doctest.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include "fixture.h"

namespace {
void dump(lv_obj_t *o, int depth) {
    lv_area_t a; lv_obj_get_coords(o, &a);
    std::printf("%*s%s %d,%d-%d,%d hidden=%d opa=%d radius=%d bg_opa=%d clip=%d\n", depth * 2, "",
                (lv_obj_check_type(o, &lv_label_class) ? "label" : lv_obj_check_type(o, &lv_arc_class) ? "arc" : "obj"), (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2,
                lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN), lv_obj_get_style_opa(o, LV_PART_MAIN), (int)lv_obj_get_style_radius(o, LV_PART_MAIN),
                lv_obj_get_style_bg_opa(o, LV_PART_MAIN), lv_obj_get_style_clip_corner(o, LV_PART_MAIN));
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) dump(lv_obj_get_child(o, i), depth + 1);
}
}

TEST_CASE("PROBE eyes page objects and render time")
{
    AppFixture f(466);
    sim::fill_demo(f.store);
    f.time_set = true;
    f.app->show(ui::App::kEyes);
    for (int i = 0; i < 50; i++) { lv_timer_handler(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    dump(lv_screen_active(), 0);
    std::printf("top layer:\n"); dump(lv_layer_top(), 0);
    std::printf("sys layer:\n"); dump(lv_layer_sys(), 0);
    auto bench = [&](const char *name) {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; i++) { lv_obj_invalidate(lv_screen_active()); lv_refr_now(f.screen.display()); }
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%-22s %lld us per full frame\n", name, (long long)(us / 100));
    };
    lv_obj_t *eyes = lv_obj_get_child(lv_screen_active(), 0);
    bench("eyes page");
    lv_obj_add_flag(eyes, LV_OBJ_FLAG_HIDDEN);
    bench("without the eyes");
    lv_obj_remove_flag(eyes, LV_OBJ_FLAG_HIDDEN);
    for (const char *m : {"neutral", "angry", "sad", "happy", "sleepy"}) {
        f.app->eyes()->set_mood(m); f.app->eyes()->snap();
        bench(m);
    }
}
