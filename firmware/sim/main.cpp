// buddy_sim: the firmware UI in a 412×412 window. The mouse acts as the touch screen.
#include <SDL2/SDL.h>

#include "lvgl.h"
#include "ui/app.h"

namespace {
uint32_t tick()
{
    return SDL_GetTicks();
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
    lv_sdl_window_set_title(disp, "Desk Buddy (simulator)");
    lv_sdl_mouse_create();

    ui::App::create({.version = "sim", .board = "sim", .seed = SDL_GetTicks()});

    for (;;) {
        const uint32_t wait = lv_timer_handler(); // also handles SDL events; closing the window exits
        SDL_Delay(wait < 5 ? wait : 5);
    }
}
