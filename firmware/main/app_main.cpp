// Desk Buddy firmware entry point: bring up the board and start the UI.
#include "board.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "ui/app.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Desk Buddy %s starting", app->version);

    ESP_ERROR_CHECK(board_init());

    if (board_lvgl_lock(0)) {
        ui::App::create({.version = app->version, .board = BOARD_NAME, .seed = esp_random()});
        board_lvgl_unlock();
    }
    ESP_LOGI(TAG, "UI running");
}
