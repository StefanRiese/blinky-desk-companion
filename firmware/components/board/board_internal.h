// What the board files share, in board_common.c: the I²C bus, the LVGL port with its
// display and touch, rotation, and the getters of board.h. Only the board files include it.
#pragma once

#include "board.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

// board_common_i2c opens I²C port 0 on these pins; board_i2c_bus() returns it.
esp_err_t board_common_i2c(gpio_num_t sda, gpio_num_t scl);

// board_common_lvgl starts the LVGL task and adds the display (two DMA buffers of
// BOARD_LVGL_LINES lines, big-endian RGB565, a rotation buffer when rotation_clockwise is
// not 0) and then the touch, if init_touch finds it. rounder aligns redrawn areas as the
// panel needs them. Ends with the backlight at 200.
esp_err_t board_common_lvgl(int rotation_clockwise, esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel,
                            void (*rounder)(lv_area_t *area), esp_err_t (*init_touch)(esp_lcd_touch_handle_t *touch));

// LVGL draws in strips of this many lines; also the size of one QSPI transfer.
#define BOARD_LVGL_LINES 40
