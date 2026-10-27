#include "audio/audio.h"

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

#include "board.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace audio {
namespace {

constexpr const char *kTag = "audio";
constexpr int kSpeakerRate = 24000;
constexpr int kMicRate = 16000;
constexpr double kVolume = 0.25;           // of full scale: loud enough to wake up to
constexpr size_t kSpeakerChunk = 480;      // 20 ms
constexpr size_t kMicChunk = kMicRate / 10; // 100 ms
constexpr uint32_t kNoiseTtlMs = 10000;

struct Audio {
    core::DataStore *store = nullptr;
    const core::Settings *settings = nullptr;
    std::function<void(core::SoundLevel::Event)> on_sound;
    i2s_chan_handle_t tx = nullptr, rx = nullptr;
    TaskHandle_t speaker = nullptr;
};
Audio g;
std::atomic<core::Tune> g_tune{core::Tune::None};
std::atomic<bool> g_playing{false};

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// open_port sets up one I²S port in standard (Philips) mode, mono, 32-bit samples.
i2s_chan_handle_t open_port(int port, bool tx, int rate, i2s_std_slot_mask_t slot, gpio_num_t bclk,
                            gpio_num_t ws, gpio_num_t data)
{
    i2s_chan_config_t chan = {};
    chan.id = port;
    chan.role = I2S_ROLE_MASTER;
    chan.dma_desc_num = 6;
    chan.dma_frame_num = 240;
    i2s_chan_handle_t h = nullptr;
    if (i2s_new_channel(&chan, tx ? &h : nullptr, tx ? nullptr : &h) != ESP_OK) {
        return nullptr;
    }
    i2s_std_config_t cfg = {};
    cfg.clk_cfg.sample_rate_hz = static_cast<uint32_t>(rate);
    cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
    cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    cfg.slot_cfg.slot_mask = slot;
    cfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_32BIT;
    cfg.slot_cfg.bit_shift = true;
    cfg.slot_cfg.left_align = true;
    cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    cfg.gpio_cfg.bclk = bclk;
    cfg.gpio_cfg.ws = ws;
    cfg.gpio_cfg.dout = tx ? data : I2S_GPIO_UNUSED;
    cfg.gpio_cfg.din = tx ? I2S_GPIO_UNUSED : data;
    if (i2s_channel_init_std_mode(h, &cfg) != ESP_OK) {
        i2s_del_channel(h);
        return nullptr;
    }
    return h;
}

void speaker_task(void *)
{
    std::array<int32_t, kSpeakerChunk> buf{};
    core::Tune playing = core::Tune::None;
    uint64_t n = 0;
    for (;;) {
        const core::Tune want = g_tune;
        if (want == core::Tune::None) {
            if (playing != core::Tune::None) {
                buf.fill(0); // end on silence, then stop the clock
                size_t written = 0;
                i2s_channel_write(g.tx, buf.data(), sizeof buf, &written, 100);
                i2s_channel_disable(g.tx);
                playing = core::Tune::None;
                g_playing = false;
            }
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (playing == core::Tune::None) {
            i2s_channel_enable(g.tx);
            g_playing = true;
        }
        if (want != playing) {
            playing = want;
            n = 0;
        }
        core::synth(playing, n, kSpeakerRate, kVolume, buf);
        n += buf.size();
        size_t written = 0;
        i2s_channel_write(g.tx, buf.data(), sizeof buf, &written, 1000);
    }
}

void mic_task(void *)
{
    std::vector<int32_t> buf(kMicChunk);
    core::SoundLevel level;
    bool on = false;
    for (;;) {
        if (!g.settings->flag("sound.enabled")) {
            if (on) {
                i2s_channel_disable(g.rx);
                on = false;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!on) {
            i2s_channel_enable(g.rx);
            on = true;
        }
        size_t got = 0;
        if (i2s_channel_read(g.rx, buf.data(), buf.size() * sizeof(int32_t), &got, 1000) != ESP_OK || got == 0) {
            continue;
        }
        if (g_playing) {
            continue; // our own beeps are no reason to be startled
        }
        const double db = core::level_db({buf.data(), got / sizeof(int32_t)});
        const auto ev = level.add(db, now_ms(), g.settings->number("sound.loud"));
        g.store->put("indoor.noise", std::round(level.average()), core::Origin::Local, kNoiseTtlMs);
        if (ev != core::SoundLevel::Event::None && g.on_sound) {
            g.on_sound(ev);
        }
    }
}

} // namespace

void start(core::DataStore &store, const core::Settings &settings,
           std::function<void(core::SoundLevel::Event)> on_sound)
{
    g.store = &store;
    g.settings = &settings;
    g.on_sound = std::move(on_sound);
    g.tx = open_port(I2S_NUM_0, true, kSpeakerRate, I2S_STD_SLOT_LEFT, BOARD_SPK_BCLK, BOARD_SPK_WS, BOARD_SPK_DOUT);
    g.rx = open_port(I2S_NUM_1, false, kMicRate, I2S_STD_SLOT_RIGHT, BOARD_MIC_SCK, BOARD_MIC_WS, BOARD_MIC_DIN);
    if (g.tx != nullptr) {
        xTaskCreatePinnedToCore(speaker_task, "speaker", 4096, nullptr, 6, &g.speaker, 0);
    } else {
        ESP_LOGE(kTag, "speaker not available");
    }
    if (g.rx != nullptr) {
        xTaskCreatePinnedToCore(mic_task, "mic", 4096, nullptr, 2, nullptr, 0);
    } else {
        ESP_LOGE(kTag, "microphone not available");
    }
}

void play(core::Tune tune)
{
    if (g_tune.exchange(tune) != tune && g.speaker != nullptr) {
        xTaskNotifyGive(g.speaker);
    }
}

} // namespace audio
