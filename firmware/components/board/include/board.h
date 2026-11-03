// Board support: one API, one implementation per board, chosen at build time (Kconfig
// "Blinky board", set by `idf.py -D BOARD=…`, see the firmware README):
// - ws146: Waveshare ESP32-S3-Touch-LCD-1.46, round 412×412 SPD2010 LCD (QSPI), SPD2010
//   touch, TCA9554 for the resets, PWM backlight, power latch, PCM5101 speaker, I²S mic
// - amoled175: Waveshare ESP32-S3-Touch-AMOLED-1.75, round 466×466 CO5300 AMOLED (QSPI),
//   CST9217 touch, AXP2101 power management (battery), ES8311 speaker, ES7210 mics
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "lvgl.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_BLINKY_BOARD_AMOLED175
#define BOARD_NAME "amoled175"
#define BOARD_LCD_H_RES 466
#define BOARD_LCD_V_RES 466
#define BOARD_MIC_SPL_AT_FULL_SCALE 120.0 // ES7210 at 30 dB gain; rough, check on the board
#else
#define BOARD_NAME "ws146"
#define BOARD_LCD_H_RES 412
#define BOARD_LCD_V_RES 412
#define BOARD_MIC_SPL_AT_FULL_SCALE 120.0 // MSM261 mic: −26 dBFS at 94 dB SPL
#endif

// Audio runs at one rate on every board: on the AMOLED the speaker and the mics share one
// I²S port, so both directions need the same clock. 16 kHz is plenty for beeps and levels.
#define BOARD_AUDIO_RATE 16000

// board_init brings up power, I²C, display, touch, backlight and the LVGL task.
// Afterwards UI code must hold board_lvgl_lock() while it touches LVGL objects.
// rotation_clockwise is ui.rotation at start: only a turned picture gets the display port's
// rotation buffer (one more draw buffer in internal RAM).
esp_err_t board_init(int rotation_clockwise);

// board_display returns the LVGL display (valid after board_init).
lv_display_t *board_display(void);

bool board_lvgl_lock(uint32_t timeout_ms);
void board_lvgl_unlock(void);

// board_set_backlight sets the brightness, 0 (off) … 255 (full).
void board_set_backlight(uint8_t level);

// board_set_rotation turns the picture (and the touch) by 0, 90, 180 or 270 degrees
// clockwise (ui.rotation). It rotates in software, so it works on every panel. Call it from
// the LVGL task (or with LVGL locked). It returns false if the picture was not turned at
// start (no rotation buffer): then it takes a restart.
bool board_set_rotation(int degrees_clockwise);

// board_i2c_bus returns the shared I²C bus for sensors and the RTC.
i2c_master_bus_handle_t board_i2c_bus(void);

// Audio, mono 16-bit samples at BOARD_AUDIO_RATE. board_audio_init returns
// ESP_ERR_NOT_SUPPORTED on a board without audio; the other calls then fail too.
esp_err_t board_audio_init(void);
esp_err_t board_speaker_enable(bool on);
esp_err_t board_speaker_write(const int16_t *samples, size_t n);
esp_err_t board_mic_enable(bool on);
// board_mic_read blocks until n samples are there (or about a second has passed).
esp_err_t board_mic_read(int16_t *samples, size_t n, size_t *got);

// board_battery returns the charge in percent, or -1 without a battery gauge (or battery);
// charging is set while it charges.
int board_battery(bool *charging);

#ifdef __cplusplus
}
#endif
