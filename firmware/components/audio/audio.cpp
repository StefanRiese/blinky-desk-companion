#include "audio/audio.h"

#include <array>
#include <atomic>
#include <cmath>

#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace audio {
namespace {

constexpr const char *kTag = "audio";
constexpr double kVolume = 0.25;                         // of full scale: loud enough to wake up to
constexpr size_t kSpeakerChunk = BOARD_AUDIO_RATE / 50;  // 20 ms
constexpr size_t kMicChunk = BOARD_AUDIO_RATE / 10;      // 100 ms
constexpr uint32_t kNoiseTtlMs = 10000;

struct Audio {
    core::DataStore *store = nullptr;
    const core::Settings *settings = nullptr;
    std::function<void(core::SoundLevel::Event)> on_sound;
    TaskHandle_t speaker = nullptr;
};
Audio g;
std::atomic<core::Tune> g_tune{core::Tune::None};
std::atomic<bool> g_playing{false};

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void speaker_task(void *)
{
    std::array<int16_t, kSpeakerChunk> buf{};
    core::Tune playing = core::Tune::None;
    uint64_t n = 0;
    for (;;) {
        const core::Tune want = g_tune;
        if (want == core::Tune::None) {
            if (playing != core::Tune::None) {
                buf.fill(0); // end on silence, then switch off
                board_speaker_write(buf.data(), buf.size());
                board_speaker_enable(false);
                playing = core::Tune::None;
                g_playing = false;
            }
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (playing == core::Tune::None) {
            board_speaker_enable(true);
            g_playing = true;
        }
        if (want != playing) {
            playing = want;
            n = 0;
        }
        core::synth(playing, n, BOARD_AUDIO_RATE, kVolume, buf);
        n += buf.size();
        board_speaker_write(buf.data(), buf.size());
    }
}

void mic_task(void *)
{
    static std::array<int16_t, kMicChunk> buf{};
    core::SoundLevel level;
    bool on = false;
    for (;;) {
        if (!g.settings->flag("sound.enabled")) {
            if (on) {
                board_mic_enable(false);
                on = false;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!on) {
            on = board_mic_enable(true) == ESP_OK;
            if (!on) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }
        size_t got = 0;
        if (board_mic_read(buf.data(), buf.size(), &got) != ESP_OK || got == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (g_playing) {
            continue; // our own beeps are no reason to be startled
        }
        const double db = core::level_db({buf.data(), got}, BOARD_MIC_SPL_AT_FULL_SCALE);
        const auto ev = level.add(db, now_ms(), g.settings->number("sound.loud"));
        g.store->put("indoor.noise", std::round(level.average()), core::Origin::Local, kNoiseTtlMs);
        if (ev != core::SoundLevel::Event::None && g.on_sound) {
            g.on_sound(ev);
        }
    }
}

} // namespace

bool start(core::DataStore &store, const core::Settings &settings,
           std::function<void(core::SoundLevel::Event)> on_sound)
{
    g.store = &store;
    g.settings = &settings;
    g.on_sound = std::move(on_sound);
    const esp_err_t err = board_audio_init();
    if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(kTag, "%s has no speaker or microphone", BOARD_NAME);
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "speaker and microphone not available (%s)", esp_err_to_name(err));
        return false;
    }
    xTaskCreatePinnedToCore(speaker_task, "speaker", 4096, nullptr, 6, &g.speaker, 0);
    xTaskCreatePinnedToCore(mic_task, "mic", 4096, nullptr, 2, nullptr, 0);
    return true;
}

void play(core::Tune tune)
{
    if (g_tune.exchange(tune) != tune && g.speaker != nullptr) {
        xTaskNotifyGive(g.speaker);
    }
}

} // namespace audio
