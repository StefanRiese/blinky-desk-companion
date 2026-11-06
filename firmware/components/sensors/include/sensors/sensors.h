// Sensors and RTC on the I²C bus (device only):
//   SHT40 (0x44) indoor temperature/humidity → DataStore indoor.temp / indoor.hum
//   PCF85063 (0x51) real-time clock: keeps the time while the buddy has no WiFi or host
//   QMI8658 (0x6B or 0x6A) IMU: shake and flick gestures (core::MotionDetector)
#pragma once

#include <ctime>
#include <functional>
#include <optional>

#include "app_core/data_store.h"
#include "app_core/motion.h"
#include "app_core/settings.h"
#include "driver/i2c_master.h"

namespace sensors {

// start starts the sensor task. Every 10 s it reads the SHT40, averages the last 6
// readings, adds sensor.temp_offset / sensor.hum_offset and stores the values (local, valid
// 60 s); without a sensor it logs once and keeps trying. If the QMI8658 is there, it reads it
// 100 times a second and calls on_gesture (from that task) for each gesture.
void start(i2c_master_bus_handle_t bus, core::DataStore &store, const core::Settings &settings,
           std::function<void(core::Gesture)> on_gesture);

class Rtc {
public:
    bool init(i2c_master_bus_handle_t bus);
    // read returns UTC from the RTC, or nullopt if it lost the time.
    std::optional<std::tm> read();
    bool write(const std::tm &utc);

    // restore_system_time sets the system clock from the RTC at boot; returns whether it did.
    bool restore_system_time();

private:
    i2c_master_dev_handle_t dev_ = nullptr;
};

} // namespace sensors
