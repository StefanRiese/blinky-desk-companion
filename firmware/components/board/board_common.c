// Board code that is the same on every board: the I²C bus, the LVGL port with the display
// and touch, rotation, and the getters of board.h. The board files (board_ws146.c,
// board_amoled175.c) bring up their own power, panel, touch and audio and call these.
#include "board_internal.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "board";

static lv_display_t *s_display;
static bool s_sw_rotate; // the port has a rotation buffer (ui.rotation was not 0 at start)
static i2c_master_bus_handle_t s_i2c;

esp_err_t board_common_i2c(gpio_num_t sda, gpio_num_t scl)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c);
}

esp_err_t board_common_lvgl(int rotation_clockwise, esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel,
                            void (*rounder)(lv_area_t *area), esp_err_t (*init_touch)(esp_lcd_touch_handle_t *touch))
{
    s_sw_rotate = (rotation_clockwise / 90) % 4 != 0;

    // The LVGL task on core 1: WiFi, TLS, the network and the sensor tasks run on core 0, so
    // a TLS handshake doesn't hold up drawing.
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_affinity = 1;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "LVGL port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BOARD_LCD_H_RES * BOARD_LVGL_LINES,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .rounder_cb = rounder,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .swap_bytes = true, // both panels expect big-endian RGB565
            .sw_rotate = s_sw_rotate, // ui.rotation: the port turns each area (needs a buffer)
        },
    };
    s_display = lvgl_port_add_disp(&disp_cfg);
    if (s_display == NULL) {
        return ESP_FAIL;
    }

    esp_lcd_touch_handle_t touch = NULL;
    if (init_touch(&touch) == ESP_OK) {
        const lvgl_port_touch_cfg_t touch_cfg = {.disp = s_display, .handle = touch};
        lvgl_port_add_touch(&touch_cfg);
    } else {
        ESP_LOGW(TAG, "touch not available, continuing without it");
    }

    board_set_backlight(200);
    ESP_LOGI(TAG, "%s ready: %dx%d", BOARD_NAME, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

// board_set_rotation: LVGL counts counterclockwise, the setting clockwise (as
// ui::display_rotation). Each board's rounder aligns both axes, so turned areas stay aligned.
bool board_set_rotation(int degrees_clockwise)
{
    static const lv_display_rotation_t kRotation[] = {LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_270,
                                                      LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_90};
    const int quarter = ((degrees_clockwise / 90) % 4 + 4) % 4;
    if (quarter != 0 && !s_sw_rotate) {
        return false; // no rotation buffer: needs a restart
    }
    if (s_display != NULL) {
        lv_display_set_rotation(s_display, kRotation[quarter]);
    }
    return true;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_i2c;
}

lv_display_t *board_display(void)
{
    return s_display;
}

bool board_lvgl_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void board_lvgl_unlock(void)
{
    lvgl_port_unlock();
}
