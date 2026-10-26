// Desk Buddy firmware entry point: settings, data store, board, clock, sensors, UI.
#include <cstdlib>
#include <ctime>
#include <string>

#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "app_core/settings_nvs.h"
#include "app_core/time_zone.h"
#include "board.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "sensors/sensors.h"
#include "ui/app.h"

static const char *TAG = "main";

namespace {

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// local_time returns the local time once the clock is set (from the RTC now; from NTP or a
// host later). Before that the system time starts at 1970.
bool local_time(std::tm &t)
{
    const std::time_t now = std::time(nullptr);
    if (now < 1700000000) { // before November 2023: not set
        return false;
    }
    return localtime_r(&now, &t) != nullptr;
}

void apply_time_zone(const core::Settings &settings)
{
    const std::string iana = settings.get("location.tz");
    const auto rule = core::posix_tz(iana);
    if (!rule) {
        ESP_LOGW(TAG, "time zone %s not in the table yet, using UTC", iana.c_str());
    }
    setenv("TZ", std::string(rule.value_or("UTC0")).c_str(), 1);
    tzset();
}

} // namespace

extern "C" void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Desk Buddy %s starting", app->version);

    if (!core::NvsStorage::init()) {
        ESP_LOGE(TAG, "NVS not available; settings will not be saved");
    }
    static core::NvsStorage storage;
    static core::Settings settings(storage);
    static core::DataStore store(now_ms);
    apply_time_zone(settings);

    ESP_ERROR_CHECK(board_init());

    static sensors::Rtc rtc;
    if (rtc.init(board_i2c_bus())) {
        rtc.restore_system_time();
    }
    sensors::start_climate(board_i2c_bus(), store, settings);

    if (board_lvgl_lock(0)) {
        ui::App::create({.version = app->version,
                         .board = BOARD_NAME,
                         .seed = esp_random(),
                         .store = &store,
                         .settings = &settings,
                         .local_time = local_time});
        board_lvgl_unlock();
    }
    ESP_LOGI(TAG, "UI running");
}
