// Desk Buddy firmware entry point. M0: bring up the board and show the hello screen.
#include "board.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "ui/hello.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Desk Buddy %s starting", app->version);

    ESP_ERROR_CHECK(board_init());

    if (board_lvgl_lock(0)) {
        ui::create_hello(lv_screen_active(), {.version = app->version, .board = BOARD_NAME});
        board_lvgl_unlock();
    }
    ESP_LOGI(TAG, "UI running");
}
