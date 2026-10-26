// Board bring-up for the Waveshare ESP32-S3-Touch-LCD-1.46. Written in C: the ESP-IDF and
// driver config macros use designated initializers in an order C++ does not accept.
// Pins and the init sequence follow Waveshare's examples and xiaozhi-esp32's board file.
#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_spd2010.h"
#include "esp_lcd_touch_spd2010.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

// Power latch: the board keeps itself on while this pin is high (on battery).
#define PIN_POWER_HOLD GPIO_NUM_7

// I²C bus shared by the IO expander and the touch controller (and later IMU, RTC).
#define PIN_I2C_SDA GPIO_NUM_11
#define PIN_I2C_SCL GPIO_NUM_10

// QSPI display
#define LCD_HOST SPI2_HOST
#define PIN_LCD_PCLK GPIO_NUM_40
#define PIN_LCD_CS GPIO_NUM_21
#define PIN_LCD_D0 GPIO_NUM_46
#define PIN_LCD_D1 GPIO_NUM_45
#define PIN_LCD_D2 GPIO_NUM_42
#define PIN_LCD_D3 GPIO_NUM_41
#define PIN_LCD_BACKLIGHT GPIO_NUM_5
#define LCD_PCLK_HZ (40 * 1000 * 1000)

// Touch interrupt (reset is on the IO expander)
#define PIN_TOUCH_INT GPIO_NUM_4

// IO expander pins: EXIO0 = display reset, EXIO1 = touch reset
#define EXIO_RESETS (IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1)

// LVGL draws into two partial buffers of 40 lines in internal DMA RAM.
#define LVGL_BUFFER_LINES 40

static lv_display_t *s_display;

static esp_err_t init_power(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_POWER_HOLD,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "power pin");
    return gpio_set_level(PIN_POWER_HOLD, 1);
}

static esp_err_t init_i2c(i2c_master_bus_handle_t *bus)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, bus);
}

// reset_display_and_touch pulses the reset lines through the TCA9554.
static esp_err_t reset_display_and_touch(i2c_master_bus_handle_t bus)
{
    esp_io_expander_handle_t exp = NULL;
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca9554(bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &exp),
                        TAG, "TCA9554");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(exp, EXIO_RESETS, IO_EXPANDER_OUTPUT), TAG, "TCA9554 dir");
    esp_io_expander_set_level(exp, EXIO_RESETS, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_io_expander_set_level(exp, EXIO_RESETS, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_io_expander_set_level(exp, EXIO_RESETS, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

static esp_err_t init_display(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel)
{
    const spi_bus_config_t bus = SPD2010_PANEL_BUS_QSPI_CONFIG(PIN_LCD_PCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2,
                                                               PIN_LCD_D3,
                                                               BOARD_LCD_H_RES * LVGL_BUFFER_LINES * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "QSPI bus");

    esp_lcd_panel_io_spi_config_t io_cfg = SPD2010_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    io_cfg.pclk_hz = LCD_PCLK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, io), TAG, "panel IO");

    const spd2010_vendor_config_t vendor = {.flags.use_qspi_interface = 1};
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC, // reset is on the IO expander
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_spd2010(*io, &panel_cfg, panel), TAG, "SPD2010");
    esp_lcd_panel_reset(*panel);
    esp_lcd_panel_init(*panel);
    return esp_lcd_panel_disp_on_off(*panel, true);
}

static esp_err_t init_touch(i2c_master_bus_handle_t bus, esp_lcd_touch_handle_t *touch)
{
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_SPD2010_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "touch IO");
    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC, // on the IO expander
        .int_gpio_num = PIN_TOUCH_INT,
    };
    return esp_lcd_touch_new_i2c_spd2010(io, &cfg, touch);
}

static esp_err_t init_backlight(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    const ledc_channel_config_t ch = {
        .gpio_num = PIN_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    return ledc_channel_config(&ch);
}

void board_set_backlight(uint8_t level)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// rounder widens each area LVGL redraws to the SPD2010's 4-pixel column alignment.
static void rounder(lv_area_t *area)
{
    area->x1 = (area->x1 >> 2) << 2;
    area->x2 = ((area->x2 >> 2) << 2) + 3;
}

esp_err_t board_init(void)
{
    ESP_RETURN_ON_ERROR(init_power(), TAG, "power");
    ESP_RETURN_ON_ERROR(init_backlight(), TAG, "backlight");

    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(init_i2c(&bus), TAG, "I2C");
    ESP_RETURN_ON_ERROR(reset_display_and_touch(bus), TAG, "resets");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(init_display(&io, &panel), TAG, "display");

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "LVGL port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BOARD_LCD_H_RES * LVGL_BUFFER_LINES,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .rounder_cb = rounder,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .swap_bytes = true, // the SPD2010 expects big-endian RGB565
        },
    };
    s_display = lvgl_port_add_disp(&disp_cfg);
    if (s_display == NULL) {
        return ESP_FAIL;
    }

    esp_lcd_touch_handle_t touch = NULL;
    if (init_touch(bus, &touch) == ESP_OK) {
        const lvgl_port_touch_cfg_t touch_cfg = {.disp = s_display, .handle = touch};
        lvgl_port_add_touch(&touch_cfg);
    } else {
        ESP_LOGW(TAG, "touch not available, continuing without it");
    }

    board_set_backlight(200);
    ESP_LOGI(TAG, "%s ready: %dx%d", BOARD_NAME, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
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
