// A headless screen with a full app (data store, settings in memory) for UI tests.
#pragma once

#include <string>
#include <vector>

#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "demo.h"
#include "headless.h"
#include "ui/app.h"

struct AppFixture {
    explicit AppFixture(int size = 412) : screen(size, size) {}

    sim::Headless screen; // first: starts LVGL
    core::DataStore store{[] { return lv_tick_get(); }};
    core::MemoryStorage storage;
    core::Settings settings{storage};
    bool time_set = false;
    core::Status status; // nothing connected
    std::vector<int> backlight; // levels set by the app
    std::vector<std::string> events; // "kind name state" the app reported
    ui::App *app = ui::App::create({.version = "test", .board = "test", .seed = 1, .store = &store,
                                    .settings = &settings,
                                    .local_time = [this](std::tm &t) { return time_set && sim::fixed_time(t); },
                                    .gestures = {},
                                    .status = [this] { return status; },
                                    .backlight = [this](int level) { backlight.push_back(level); },
                                    .event =
                                        [this](std::string_view k, std::string_view n, std::string_view s) {
                                            events.push_back(std::string(k) + " " + std::string(n) + " " +
                                                             std::string(s));
                                        }});

    // swipe drags across the screen and waits for the slide animation.
    void swipe(int from_x, int to_x);

    int alarms_on()
    {
        int n = 0;
        for (const auto &a : app->alarms().alarms()) {
            n += a.on;
        }
        return n;
    }
};
