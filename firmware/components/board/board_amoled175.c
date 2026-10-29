// Board bring-up for the Waveshare ESP32-S3-Touch-AMOLED-1.75 (plain, no GPS). Written in C
// like board_ws146.c. Pins, the AXP2101 settings, the CO5300 init sequence and the codec
// setup follow Waveshare's wiki and xiaozhi-esp32's board file for this board.
#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

// I²C bus: touch, AXP2101, ES8311, ES7210, QMI8658, PCF85063, TCA9554 (and the SHT40).
#define PIN_I2C_SDA GPIO_NUM_15
#define PIN_I2C_SCL GPIO_NUM_14
#define AXP2101_ADDR 0x34

// QSPI display (CO5300, 466×466; its columns start 6 pixels in)
#define LCD_HOST SPI2_HOST
#define PIN_LCD_CS GPIO_NUM_12
#define PIN_LCD_PCLK GPIO_NUM_38
#define PIN_LCD_D0 GPIO_NUM_4
#define PIN_LCD_D1 GPIO_NUM_5
#define PIN_LCD_D2 GPIO_NUM_6
#define PIN_LCD_D3 GPIO_NUM_7
#define PIN_LCD_RST GPIO_NUM_39
#define LCD_PCLK_HZ (40 * 1000 * 1000)
#define LCD_COLUMN_GAP 6
#define LCD_OPCODE_WRITE_CMD 0x02

// Touch (CST9217)
#define PIN_TOUCH_RST GPIO_NUM_40
#define PIN_TOUCH_INT GPIO_NUM_11

// Audio: ES8311 (speaker, amplifier enable on GPIO 46) and ES7210 (mics) share one I²S port.
#define PIN_I2S_MCLK GPIO_NUM_42
#define PIN_I2S_BCLK GPIO_NUM_9
#define PIN_I2S_WS GPIO_NUM_45
#define PIN_I2S_DOUT GPIO_NUM_8
#define PIN_I2S_DIN GPIO_NUM_10
#define PIN_PA GPIO_NUM_46
#define SPEAKER_VOLUME 70 // 0 … 100
#define MIC_GAIN_DB 30.0f

#define LVGL_BUFFER_LINES 40

static lv_display_t *s_display;
static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_pmic;
static esp_lcd_panel_io_handle_t s_lcd_io;
static esp_codec_dev_handle_t s_speaker, s_mic;

static esp_err_t init_i2c(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c);
}

// ---------------------------------------------------------------- AXP2101

static esp_err_t pmic_write(uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_pmic, buf, sizeof buf, 100);
}

static int pmic_read(uint8_t reg)
{
    uint8_t value = 0;
    if (s_pmic == NULL || i2c_master_transmit_receive(s_pmic, &reg, 1, &value, 1, 100) != ESP_OK) {
        return -1;
    }
    return value;
}

// init_pmic: DC1 at 3.3 V for the board, ALDO1 at 3.3 V for the microphones, everything else
// off; power key: hold 4 s to switch off; charging at 4.1 V, 200 mA.
static esp_err_t init_pmic(void)
{
    const i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dev, &s_pmic), TAG, "AXP2101");
    const uint8_t regs[][2] = {
        {0x22, 0x06},                // PWRON > OFFLEVEL switches off
        {0x27, 0x10},                // hold the power key 4 s to switch off
        {0x80, 0x01},                // DCDCs: only DC1
        {0x90, 0x00},                // LDOs off …
        {0x91, 0x00},
        {0x82, (3300 - 1500) / 100}, // DC1 3.3 V
        {0x92, (3300 - 500) / 100},  // ALDO1 3.3 V
        {0x90, 0x01},                // … but ALDO1 (microphones) on
        {0x64, 0x02},                // charge to 4.1 V
        {0x61, 0x02},                // precharge 50 mA
        {0x62, 0x08},                // charge 200 mA
        {0x63, 0x01},                // end of charge at 25 mA
    };
    for (size_t i = 0; i < sizeof regs / sizeof regs[0]; i++) {
        ESP_RETURN_ON_ERROR(pmic_write(regs[i][0], regs[i][1]), TAG, "AXP2101 register 0x%02x", regs[i][0]);
    }
    return ESP_OK;
}

int board_battery(bool *charging)
{
    const int status = pmic_read(0x00), direction = pmic_read(0x01), level = pmic_read(0xA4);
    if (charging != NULL) {
        *charging = direction >= 0 && ((direction >> 5) & 0x03) == 1;
    }
    if (status < 0 || (status & 0x08) == 0 || level < 0) { // no battery connected
        return -1;
    }
    return level > 100 ? 100 : level;
}

// ---------------------------------------------------------------- display and touch

static const co5300_lcd_init_cmd_t s_lcd_init[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0}, // QSPI mode
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0}, // RGB565
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0}, // brightness
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0}, // columns 6 … 471
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600}, // rows 0 … 465
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

static esp_err_t init_display(esp_lcd_panel_handle_t *panel)
{
    const spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_PCLK,
        .data0_io_num = PIN_LCD_D0,
        .data1_io_num = PIN_LCD_D1,
        .data2_io_num = PIN_LCD_D2,
        .data3_io_num = PIN_LCD_D3,
        .max_transfer_sz = BOARD_LCD_H_RES * LVGL_BUFFER_LINES * sizeof(uint16_t),
        .flags = SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "QSPI bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = GPIO_NUM_NC,
        .spi_mode = 0,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_lcd_io), TAG,
                        "panel IO");

    const co5300_vendor_config_t vendor = {
        .init_cmds = s_lcd_init,
        .init_cmds_size = sizeof s_lcd_init / sizeof s_lcd_init[0],
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_lcd_io, &panel_cfg, panel), TAG, "CO5300");
    esp_lcd_panel_set_gap(*panel, LCD_COLUMN_GAP, 0);
    esp_lcd_panel_reset(*panel);
    esp_lcd_panel_init(*panel);
    return esp_lcd_panel_disp_on_off(*panel, true);
}

static esp_err_t init_touch(esp_lcd_touch_handle_t *touch)
{
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c, &io_cfg, &io), TAG, "touch IO");
    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_H_RES - 1,
        .y_max = BOARD_LCD_V_RES - 1,
        .rst_gpio_num = PIN_TOUCH_RST,
        .int_gpio_num = PIN_TOUCH_INT,
        .flags = {.mirror_x = 1, .mirror_y = 1},
    };
    return esp_lcd_touch_new_i2c_cst9217(io, &cfg, touch);
}

void board_set_backlight(uint8_t level)
{
    if (s_lcd_io != NULL) {
        esp_lcd_panel_io_tx_param(s_lcd_io, (LCD_OPCODE_WRITE_CMD << 24) | (0x51 << 8), &level, 1);
    }
}

// rounder: the CO5300 takes areas that start on an even and end on an odd pixel.
static void rounder(lv_area_t *area)
{
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

// board_set_rotation: LVGL counts counterclockwise, the setting clockwise (as ui::display_rotation).
// Both axes are aligned above and 466 is even, so turned areas keep the CO5300's alignment.
void board_set_rotation(int degrees_clockwise)
{
    static const lv_display_rotation_t kRotation[] = {LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_270,
                                                      LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_90};
    if (s_display != NULL) {
        lv_display_set_rotation(s_display, kRotation[((degrees_clockwise / 90) % 4 + 4) % 4]);
    }
}

esp_err_t board_init(void)
{
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "I2C");
    if (init_pmic() != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 not set up: running on its defaults");
    }

    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(init_display(&panel), TAG, "display");

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "LVGL port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_lcd_io,
        .panel_handle = panel,
        .buffer_size = BOARD_LCD_H_RES * LVGL_BUFFER_LINES,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .rounder_cb = rounder,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .swap_bytes = true, // the CO5300 expects big-endian RGB565
            .sw_rotate = true,  // ui.rotation: the port turns each area before it is sent
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

// open_i2s creates the shared I²S port: the speaker in standard mode (16-bit stereo), the
// ES7210 in TDM mode (four 16-bit slots), both at BOARD_AUDIO_RATE.
static esp_err_t open_i2s(i2s_chan_handle_t *tx, i2s_chan_handle_t *rx)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear_after_cb = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, tx, rx), TAG, "I2S channels");

    const i2s_std_config_t std_cfg = {
        .clk_cfg = {.sample_rate_hz = BOARD_AUDIO_RATE, .clk_src = I2S_CLK_SRC_DEFAULT,
                    .mclk_multiple = I2S_MCLK_MULTIPLE_256},
        .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT, .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                     .slot_mode = I2S_SLOT_MODE_STEREO, .slot_mask = I2S_STD_SLOT_BOTH,
                     .ws_width = I2S_DATA_BIT_WIDTH_16BIT, .bit_shift = true, .left_align = true},
        .gpio_cfg = {.mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS, .dout = PIN_I2S_DOUT,
                     .din = I2S_GPIO_UNUSED},
    };
    const i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {.sample_rate_hz = BOARD_AUDIO_RATE, .clk_src = I2S_CLK_SRC_DEFAULT,
                    .mclk_multiple = I2S_MCLK_MULTIPLE_256, .bclk_div = 8},
        .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT, .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                     .slot_mode = I2S_SLOT_MODE_STEREO,
                     .slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
                     .ws_width = I2S_TDM_AUTO_WS_WIDTH, .bit_shift = true, .total_slot = I2S_TDM_AUTO_SLOT_NUM},
        .gpio_cfg = {.mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS, .dout = I2S_GPIO_UNUSED,
                     .din = PIN_I2S_DIN},
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(*tx, &std_cfg), TAG, "I2S speaker");
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(*rx, &tdm_cfg), TAG, "I2S mics");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(*tx), TAG, "I2S speaker on");
    return i2s_channel_enable(*rx);
}

esp_err_t board_audio_init(void)
{
    i2s_chan_handle_t tx = NULL, rx = NULL;
    ESP_RETURN_ON_ERROR(open_i2s(&tx, &rx), TAG, "I2S");

    audio_codec_i2s_cfg_t i2s_cfg = {.port = I2S_NUM_0, .rx_handle = rx, .tx_handle = tx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    audio_codec_i2c_cfg_t i2c_cfg = {.port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = s_i2c};
    const audio_codec_ctrl_if_t *out_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    i2c_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    const audio_codec_ctrl_if_t *in_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (data_if == NULL || gpio_if == NULL || out_ctrl == NULL || in_ctrl == NULL) {
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es8311 = {
        .ctrl_if = out_ctrl,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = PIN_PA,
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    const audio_codec_if_t *out_codec = es8311_codec_new(&es8311);
    es7210_codec_cfg_t es7210 = {
        .ctrl_if = in_ctrl,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    const audio_codec_if_t *in_codec = es7210_codec_new(&es7210);
    if (out_codec == NULL || in_codec == NULL) {
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = out_codec, .data_if = data_if};
    s_speaker = esp_codec_dev_new(&dev);
    dev.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev.codec_if = in_codec;
    s_mic = esp_codec_dev_new(&dev);
    return s_speaker != NULL && s_mic != NULL ? ESP_OK : ESP_FAIL;
}

esp_err_t board_speaker_enable(bool on)
{
    if (s_speaker == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!on) {
        return esp_codec_dev_close(s_speaker) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
    }
    esp_codec_dev_sample_info_t fs = {.bits_per_sample = 16, .channel = 1, .sample_rate = BOARD_AUDIO_RATE};
    if (esp_codec_dev_open(s_speaker, &fs) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_vol(s_speaker, SPEAKER_VOLUME);
    return ESP_OK;
}

esp_err_t board_speaker_write(const int16_t *samples, size_t n)
{
    if (s_speaker == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const int rc = esp_codec_dev_write(s_speaker, (void *)samples, (int)(n * sizeof(int16_t)));
    return rc == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t board_mic_enable(bool on)
{
    if (s_mic == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!on) {
        return esp_codec_dev_close(s_mic) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
    }
    // Four TDM slots; only MIC1 is read (the level of one mic is all we need).
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 4,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = BOARD_AUDIO_RATE,
    };
    if (esp_codec_dev_open(s_mic, &fs) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    esp_codec_dev_set_in_gain(s_mic, MIC_GAIN_DB);
    return ESP_OK;
}

esp_err_t board_mic_read(int16_t *samples, size_t n, size_t *got)
{
    *got = 0;
    if (s_mic == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (esp_codec_dev_read(s_mic, samples, (int)(n * sizeof(int16_t))) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    *got = n;
    return ESP_OK;
}
