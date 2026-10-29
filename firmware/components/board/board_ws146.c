// Board bring-up for the Waveshare ESP32-S3-Touch-LCD-1.46. Written in C: the ESP-IDF and
// driver config macros use designated initializers in an order C++ does not accept.
// Pins and the init sequence follow Waveshare's examples and xiaozhi-esp32's board file.
#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
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

// Audio: PCM5101 DAC + amplifier + speaker, and an I²S microphone (MSM261S4030H0, right
// slot), on separate I²S ports, both with 32-bit slots. Pins as in xiaozhi-esp32.
#define PIN_SPK_BCLK GPIO_NUM_48
#define PIN_SPK_WS GPIO_NUM_38
#define PIN_SPK_DOUT GPIO_NUM_47
#define PIN_MIC_SCK GPIO_NUM_15
#define PIN_MIC_WS GPIO_NUM_2
#define PIN_MIC_DIN GPIO_NUM_39
#define AUDIO_CHUNK 320 // samples converted at a time (20 ms)

static lv_display_t *s_display;
static bool s_sw_rotate; // the port has a rotation buffer (ui.rotation was not 0 at start)
static i2c_master_bus_handle_t s_i2c;
static i2s_chan_handle_t s_spk, s_mic;

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

// rounder widens each area LVGL redraws to the SPD2010's 4-pixel column alignment. Rows are
// aligned the same way, so the areas stay aligned when the picture is turned by 90° (412 is
// a multiple of 4, so the mirrored side lines up too).
static void rounder(lv_area_t *area)
{
    area->x1 = (area->x1 >> 2) << 2;
    area->x2 = ((area->x2 >> 2) << 2) + 3;
    area->y1 = (area->y1 >> 2) << 2;
    area->y2 = ((area->y2 >> 2) << 2) + 3;
}
// board_set_rotation: LVGL counts counterclockwise, the setting clockwise (as ui::display_rotation).
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


esp_err_t board_init(int rotation_clockwise)
{
    s_sw_rotate = (rotation_clockwise / 90) % 4 != 0;
    ESP_RETURN_ON_ERROR(init_power(), TAG, "power");
    ESP_RETURN_ON_ERROR(init_backlight(), TAG, "backlight");

    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(init_i2c(&bus), TAG, "I2C");
    s_i2c = bus;
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
            .sw_rotate = s_sw_rotate, // ui.rotation: the port turns each area (needs a buffer)
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

// ---------------------------------------------------------------- audio

// open_i2s sets up one I²S port: standard (Philips) mode, mono, 32-bit slots.
static i2s_chan_handle_t open_i2s(int port, bool tx, i2s_std_slot_mask_t slot, gpio_num_t bclk, gpio_num_t ws,
                                  gpio_num_t data)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    i2s_chan_handle_t h = NULL;
    if (i2s_new_channel(&chan, tx ? &h : NULL, tx ? NULL : &h) != ESP_OK) {
        return NULL;
    }
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BOARD_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = bclk,
                .ws = ws,
                .dout = tx ? data : I2S_GPIO_UNUSED,
                .din = tx ? I2S_GPIO_UNUSED : data,
            },
    };
    cfg.slot_cfg.slot_mask = slot;
    if (i2s_channel_init_std_mode(h, &cfg) != ESP_OK) {
        i2s_del_channel(h);
        return NULL;
    }
    return h;
}

esp_err_t board_audio_init(void)
{
    s_spk = open_i2s(I2S_NUM_0, true, I2S_STD_SLOT_LEFT, PIN_SPK_BCLK, PIN_SPK_WS, PIN_SPK_DOUT);
    s_mic = open_i2s(I2S_NUM_1, false, I2S_STD_SLOT_RIGHT, PIN_MIC_SCK, PIN_MIC_WS, PIN_MIC_DIN);
    return s_spk != NULL && s_mic != NULL ? ESP_OK : ESP_FAIL;
}

esp_err_t board_speaker_enable(bool on)
{
    if (s_spk == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return on ? i2s_channel_enable(s_spk) : i2s_channel_disable(s_spk);
}

esp_err_t board_speaker_write(const int16_t *samples, size_t n)
{
    if (s_spk == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int32_t buf[AUDIO_CHUNK];
    while (n > 0) {
        const size_t k = n < AUDIO_CHUNK ? n : AUDIO_CHUNK;
        for (size_t i = 0; i < k; i++) {
            buf[i] = (int32_t)samples[i] << 16;
        }
        size_t written = 0;
        ESP_RETURN_ON_ERROR(i2s_channel_write(s_spk, buf, k * sizeof(int32_t), &written, 1000), TAG, "speaker");
        samples += k;
        n -= k;
    }
    return ESP_OK;
}

esp_err_t board_mic_enable(bool on)
{
    if (s_mic == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return on ? i2s_channel_enable(s_mic) : i2s_channel_disable(s_mic);
}

esp_err_t board_mic_read(int16_t *samples, size_t n, size_t *got)
{
    *got = 0;
    if (s_mic == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int32_t buf[AUDIO_CHUNK];
    while (*got < n) {
        const size_t k = n - *got < AUDIO_CHUNK ? n - *got : AUDIO_CHUNK;
        size_t bytes = 0;
        ESP_RETURN_ON_ERROR(i2s_channel_read(s_mic, buf, k * sizeof(int32_t), &bytes, 1000), TAG, "mic");
        const size_t m = bytes / sizeof(int32_t);
        for (size_t i = 0; i < m; i++) {
            samples[*got + i] = (int16_t)(buf[i] >> 16); // the mic's 24 bits sit at the top
        }
        *got += m;
        if (m == 0) {
            break;
        }
    }
    return ESP_OK;
}

// The ws146 charges a battery but has no gauge.
int board_battery(bool *charging)
{
    if (charging != NULL) {
        *charging = false;
    }
    return -1;
}
