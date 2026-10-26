#include "sensors/sensors.h"

#include <sys/time.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "app_core/sensor_math.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace sensors {
namespace {

constexpr const char *kTag = "sensors";
constexpr uint8_t kSht4xAddr = 0x44;
constexpr uint8_t kSht4xMeasureHigh = 0xFD; // high precision, ~9 ms
constexpr uint8_t kRtcAddr = 0x51;
constexpr uint8_t kRtcSeconds = 0x04; // first time register
constexpr uint32_t kPeriodMs = 10000;
constexpr uint32_t kTtlMs = 60000;

i2c_master_dev_handle_t add_device(i2c_master_bus_handle_t bus, uint8_t addr)
{
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = 100000;
    i2c_master_dev_handle_t dev = nullptr;
    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) {
        return nullptr;
    }
    return dev;
}

struct ClimateTask {
    i2c_master_dev_handle_t dev;
    core::DataStore *store;
    const core::Settings *settings;
};

std::optional<core::Climate> read_sht4x(i2c_master_dev_handle_t dev)
{
    if (i2c_master_transmit(dev, &kSht4xMeasureHigh, 1, 100) != ESP_OK) {
        return std::nullopt;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    std::array<uint8_t, 6> raw{};
    if (i2c_master_receive(dev, raw.data(), raw.size(), 100) != ESP_OK) {
        return std::nullopt;
    }
    return core::sht4x_decode(raw);
}

void climate_task(void *arg)
{
    auto *t = static_cast<ClimateTask *>(arg);
    core::MovingAverage<6> temp, hum;
    bool missing_logged = false;
    for (;;) {
        if (const auto c = read_sht4x(t->dev)) {
            missing_logged = false;
            const double tc = temp.add(c->temp_c) + t->settings->number("sensor.temp_offset");
            const double rh = std::clamp(hum.add(c->hum_pct) + t->settings->number("sensor.hum_offset"), 0.0, 100.0);
            t->store->put("indoor.temp", std::round(tc * 10) / 10, core::Origin::Local, kTtlMs);
            t->store->put("indoor.hum", std::round(rh), core::Origin::Local, kTtlMs);
        } else if (!missing_logged) {
            ESP_LOGW(kTag, "no SHT40 at 0x%02x (indoor climate unavailable)", kSht4xAddr);
            missing_logged = true;
        }
        vTaskDelay(pdMS_TO_TICKS(kPeriodMs));
    }
}

} // namespace

void start_climate(i2c_master_bus_handle_t bus, core::DataStore &store, const core::Settings &settings)
{
    auto *t = new ClimateTask{add_device(bus, kSht4xAddr), &store, &settings};
    if (t->dev == nullptr) {
        ESP_LOGE(kTag, "cannot add the SHT40 to the I2C bus");
        delete t;
        return;
    }
    xTaskCreatePinnedToCore(climate_task, "climate", 4096, t, 2, nullptr, 0);
}

bool Rtc::init(i2c_master_bus_handle_t bus)
{
    dev_ = add_device(bus, kRtcAddr);
    return dev_ != nullptr;
}

std::optional<std::tm> Rtc::read()
{
    std::array<uint8_t, 7> regs{};
    if (dev_ == nullptr ||
        i2c_master_transmit_receive(dev_, &kRtcSeconds, 1, regs.data(), regs.size(), 100) != ESP_OK) {
        return std::nullopt;
    }
    return core::pcf85063_decode(regs);
}

bool Rtc::write(const std::tm &utc)
{
    const auto regs = core::pcf85063_encode(utc);
    std::array<uint8_t, 8> buf{kRtcSeconds};
    std::copy(regs.begin(), regs.end(), buf.begin() + 1);
    return dev_ != nullptr && i2c_master_transmit(dev_, buf.data(), buf.size(), 100) == ESP_OK;
}

bool Rtc::restore_system_time()
{
    auto t = read();
    if (!t) {
        ESP_LOGW(kTag, "RTC has no valid time (yet)");
        return false;
    }
    const time_t secs = timegm(&*t);
    const timeval tv = {.tv_sec = secs, .tv_usec = 0};
    settimeofday(&tv, nullptr);
    ESP_LOGI(kTag, "system time restored from the RTC");
    return true;
}

} // namespace sensors
