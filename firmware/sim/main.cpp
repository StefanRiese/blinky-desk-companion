// buddy_sim: the firmware UI in a 412×412 window. The mouse acts as the touch screen.
// The data is a demo (cloudy morning, indoor climate drifting slowly); the clock is real.
#include <SDL2/SDL.h>

#include <cstdlib>
#include <ctime>

#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "demo.h"
#include "lvgl.h"
#include "ui/app.h"

namespace {

uint32_t tick()
{
    return SDL_GetTicks();
}

bool local_time(std::tm &t)
{
    const std::time_t now = std::time(nullptr);
    return localtime_r(&now, &t) != nullptr;
}

// Indoor climate drifting a little, like the emulator's fake sensor.
void drift(lv_timer_t *t)
{
    auto *store = static_cast<core::DataStore *>(lv_timer_get_user_data(t));
    const double temp = store->number("indoor.temp").value_or(21.6) + (std::rand() % 21 - 10) / 100.0;
    const double hum = store->number("indoor.hum").value_or(44) + (std::rand() % 11 - 5) / 10.0;
    store->put("indoor.temp", temp, core::Origin::Local, 60000);
    store->put("indoor.hum", hum, core::Origin::Local, 60000);
}

} // namespace

int main(int, char **)
{
    lv_init();
    lv_tick_set_cb(tick);

    lv_display_t *disp = lv_sdl_window_create(412, 412);
    if (disp == nullptr) {
        SDL_Log("cannot open a window (no display?); use buddy_shot for screenshots instead");
        return 1;
    }
    lv_sdl_window_set_title(disp, "Blinky (simulator)");
    lv_sdl_mouse_create();

    static core::DataStore store(tick);
    static core::MemoryStorage storage;
    static core::Settings settings(storage);
    sim::fill_demo(store);
    lv_timer_create(drift, 5000, &store);

    ui::App::create({.version = "sim", .board = "sim", .seed = SDL_GetTicks(), .store = &store, .settings = &settings,
                     .local_time = local_time,
                     .gestures = {},
                     .status = sim::demo_status,
                     .backlight = nullptr}); // a window has no backlight

    for (;;) {
        const uint32_t wait = lv_timer_handler(); // also handles SDL events; closing the window exits
        SDL_Delay(wait < 5 ? wait : 5);
    }
}
