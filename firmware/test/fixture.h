// A headless screen with a full app (data store, settings in memory) for UI tests.
#pragma once

#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "demo.h"
#include "headless.h"
#include "ui/app.h"

struct AppFixture {
    sim::Headless screen{412, 412}; // first: starts LVGL
    core::DataStore store{[] { return lv_tick_get(); }};
    core::MemoryStorage storage;
    core::Settings settings{storage};
    bool time_set = false;
    ui::App *app = ui::App::create({.version = "test", .board = "test", .seed = 1, .store = &store,
                                    .settings = &settings,
                                    .local_time = [this](std::tm &t) { return time_set && sim::fixed_time(t); }});

    // swipe drags across the screen and waits for the slide animation.
    void swipe(int from_x, int to_x);
};
