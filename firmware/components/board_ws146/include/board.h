// Board support for the Waveshare ESP32-S3-Touch-LCD-1.46: round 412×412 SPD2010 display
// (QSPI) with SPD2010 touch (I²C), TCA9554 IO expander for the resets, PWM backlight.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_NAME "ws146"
#define BOARD_LCD_H_RES 412
#define BOARD_LCD_V_RES 412

// board_init brings up power, I²C, display, touch, backlight and the LVGL task.
// Afterwards UI code must hold board_lvgl_lock() while it touches LVGL objects.
esp_err_t board_init(void);

// board_display returns the LVGL display (valid after board_init).
lv_display_t *board_display(void);

bool board_lvgl_lock(uint32_t timeout_ms);
void board_lvgl_unlock(void);

// board_set_backlight sets the backlight, 0 (off) … 255 (full).
void board_set_backlight(uint8_t level);

// board_i2c_bus returns the shared I²C bus (GPIO 11 SDA, 10 SCL) for sensors and the RTC.
i2c_master_bus_handle_t board_i2c_bus(void);

#ifdef __cplusplus
}
#endif
