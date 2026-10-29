// Blinky firmware entry point: settings, data store, board, clock, sensors, WiFi, host links
// (USB, TCP), the SourceRouter, speaker and microphone, and the UI (with the MoodEngine).
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/time.h>
#include <vector>

#include "app_core/data_store.h"
#include "app_core/device_fetcher.h"
#include "app_core/host_link.h"
#include "app_core/improv.h"
#include "app_core/ota.h"
#include "app_core/settings.h"
#include "app_core/settings_nvs.h"
#include "app_core/sound.h"
#include "app_core/source_router.h"
#include "app_core/status.h"
#include "app_core/time_zone.h"
#include "audio/audio.h"
#include "board.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "net/net.h"
#include "sensors/sensors.h"
#include "serial_link/serial_link.h"
#include "tcp_link/tcp_link.h"
#include "ui/app.h"

static const char *TAG = "main";

namespace {

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// local_time returns the local time once the clock is set (from the RTC, NTP or a host).
// Before that the system time starts at 1970.
bool local_time(std::tm &t)
{
    const std::time_t now = std::time(nullptr);
    if (now < 1700000000) { // before November 2023: not set
        return false;
    }
    return localtime_r(&now, &t) != nullptr;
}

void apply_time_zone(const core::Settings &settings)
{
    const std::string iana = settings.get("location.tz");
    const auto rule = core::posix_tz(iana);
    if (!rule) {
        ESP_LOGW(TAG, "time zone %s not in the table yet, using UTC", iana.c_str());
    }
    setenv("TZ", std::string(rule.value_or("UTC0")).c_str(), 1);
    tzset();
}

bool starts_with(const std::string &s, const char *prefix)
{
    return s.rfind(prefix, 0) == 0;
}

// Blinky holds everything that lives as long as the firmware. The host links, the router
// and the UI belong to the LVGL task; the fetcher runs in the network task.
struct Blinky {
    core::NvsStorage storage;
    core::Settings settings{storage};
    core::DataStore store{now_ms};
    sensors::Rtc rtc;
    core::DeviceFetcher fetcher{store, now_ms,
                                {.get = net::http_get, .sync_time = net::sync_time,
                                 .set_time = [this](int64_t epoch) { set_clock(epoch); }}};
    core::HostLink usb, tcp;
    // Firmware updates from a host (OtaReceiver checks them, esp_ota_* writes them).
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *ota_part = nullptr;
    core::OtaReceiver ota;
    uint32_t ota_done_at = 0;
    core::SourceRouter router{settings, {&usb, &tcp}, &fetcher};
    core::Improv improv;

    std::mutex mu;                    // the two below
    std::vector<std::string> changes; // settings saved by the web page, for the LVGL task
    std::vector<core::SoundLevel::Event> sounds; // reactions from the microphone task
    std::vector<core::Gesture> gestures;         // from the motion task (IMU)
    bool imu = false;                            // the QMI8658 was found
    core::Status status;              // snapshot for the screens and the web page

    ui::App *app = nullptr; // LVGL task only
    bool tcp_was_ready = false;

    explicit Blinky(const esp_app_desc_t *app);
    void set_clock(int64_t epoch);
    core::HostLink::Hooks link_hooks(core::HostLink *self, void (*send)(std::string_view), void (*silent)());
    core::OtaReceiver::Hooks ota_hooks();
    void changed(const std::vector<std::string> &keys, const core::HostLink *from);
    void poll();
    void react(core::SoundLevel::Event ev);
    core::Status snapshot();
};

Blinky *g = nullptr;

Blinky::Blinky(const esp_app_desc_t *app)
    : usb("usb", settings, store, {.fw = app->version, .board = BOARD_NAME, .caps = {"eyes", "touch", "sht40", "wifi", "ota"}},
          link_hooks(&usb, serial_link::write, nullptr)),
      tcp("tcp", settings, store, {.fw = app->version, .board = BOARD_NAME, .caps = {"eyes", "touch", "sht40", "wifi", "ota"}},
          link_hooks(&tcp, tcp_link::write, tcp_link::reconnect)),
      ota(ota_hooks()),
      improv({.firmware = "Blinky", .version = app->version, .chip = "ESP32-S3", .name = settings.get("dev")},
             {.send = serial_link::write_bytes,
              .connect =
                  [this](const std::string &ssid, const std::string &pass) {
                      net::connect(ssid, pass, [this](bool ok) { improv.connected(ok); });
                  },
              .scan = net::scan,
              .url = net::url})
{
}

// set_clock sets the system time and the RTC from NTP or a host's epoch.
void Blinky::set_clock(int64_t epoch)
{
    const timeval tv{.tv_sec = static_cast<time_t>(epoch), .tv_usec = 0};
    settimeofday(&tv, nullptr);
    const std::time_t t = static_cast<std::time_t>(epoch);
    std::tm utc{};
    if (gmtime_r(&t, &utc) != nullptr && !rtc.write(utc)) {
        ESP_LOGW(TAG, "RTC not updated");
    }
}

core::HostLink::Hooks Blinky::link_hooks(core::HostLink *self, void (*send)(std::string_view), void (*silent)())
{
    return {.send = send,
            .set_time = [this](int64_t epoch) { set_clock(epoch); },
            .changed = [this, self](const std::vector<std::string> &keys) { changed(keys, self); },
            .routing = [this] { router.route(); },
            .silent = silent,
            .mood =
                [this](std::string_view mood, int ttl) {
                    if (app != nullptr) {
                        app->mood().set_override(mood, static_cast<uint32_t>(ttl) * 1000, lv_tick_get());
                    }
                },
            .page =
                [this](JsonVariantConst m) {
                    if (app != nullptr) {
                        app->custom_pages().handle(m, lv_tick_get());
                    }
                },
            .notify =
                [this](const core::Notification &n) {
                    if (app != nullptr) {
                        app->notify(n);
                    }
                },
            .ota = [this](JsonVariantConst m) { return ota.handle(m); }};
}

// ota_hooks write an update to the other app partition. Sequential writes erase each
// sector just before it is written, so begin doesn't block the UI while it erases.
core::OtaReceiver::Hooks Blinky::ota_hooks()
{
    auto failed = [](const char *what, esp_err_t e) { return std::string(what) + ": " + esp_err_to_name(e); };
    return {.begin =
                [this, failed](size_t size) -> std::optional<std::string> {
                    ota_part = esp_ota_get_next_update_partition(nullptr);
                    if (ota_part == nullptr) {
                        return "no partition for updates";
                    }
                    if (size > ota_part->size) {
                        return "the image is larger than the app partition";
                    }
                    if (esp_err_t e = esp_ota_begin(ota_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle); e != ESP_OK) {
                        return failed("esp_ota_begin", e);
                    }
                    ESP_LOGI(TAG, "update: %u bytes to %s", static_cast<unsigned>(size), ota_part->label);
                    return std::nullopt;
                },
            .write = [this, failed](const uint8_t *data, size_t len) -> std::optional<std::string> {
                if (esp_err_t e = esp_ota_write(ota_handle, data, len); e != ESP_OK) {
                    return failed("esp_ota_write", e);
                }
                return std::nullopt;
            },
            .finish = [this, failed]() -> std::optional<std::string> {
                if (esp_err_t e = esp_ota_end(ota_handle); e != ESP_OK) {
                    return failed("the image is not valid", e);
                }
                if (esp_err_t e = esp_ota_set_boot_partition(ota_part); e != ESP_OK) {
                    return failed("esp_ota_set_boot_partition", e);
                }
                ESP_LOGI(TAG, "update: ready, %s boots next", ota_part->label);
                return std::nullopt;
            },
            .abort = [this] { esp_ota_abort(ota_handle); }};
}

// show_fps shows LVGL's frame rate and CPU counter at the bottom of the screen (ui.fps, for
// measuring the rendering). Call it in the LVGL task.
void show_fps(const core::Settings &settings)
{
    if (settings.get("ui.fps") == "true") {
        lv_sysmon_show_performance(board_display());
    } else {
        lv_sysmon_hide_performance(board_display());
    }
}

// changed applies settings saved by a host, the setup page or the web page (from: the link
// they came over; it re-introduces itself already).
void Blinky::changed(const std::vector<std::string> &keys, const core::HostLink *from)
{
    bool route = false, relink = false, wifi = false, tcp_target = false;
    for (const auto &k : keys) {
        route = route || k == "sources" || starts_with(k, "location.");
        relink = relink || k == "dev" || k == "link.token";
        wifi = wifi || starts_with(k, "wifi.");
        if (k == "ui.rotation") {
            board_set_rotation(std::atoi(settings.get("ui.rotation").c_str())); // in the LVGL task
        }
        if (k == "ui.fps") {
            show_fps(settings);
        }
        tcp_target = tcp_target || k == "link.tcp_host";
        if (k == "location.tz") {
            apply_time_zone(settings);
        }
    }
    const uint32_t now = now_ms();
    if (relink && from != &usb) {
        usb.relink(now);
    }
    if (tcp_target || (relink && from != &tcp)) {
        tcp_link::reconnect(); // a new connection says hello with the new name and token
    }
    if (wifi) {
        net::reconnect();
    }
    if (route) {
        router.route();
    }
}

core::Status Blinky::snapshot()
{
    core::Status s;
    const net::State w = net::state();
    s.wifi = w.wifi;
    s.wifi_text = w.text;
    s.usb = {.up = usb.ready(), .text = core::describe(usb)};
    s.tcp = {.up = tcp.ready(), .text = tcp.state() == core::HostLink::State::Off ? tcp_link::text()
                                                                                  : core::describe(tcp)};
    s.sources = router.status();
    // A TCP host that was there and is gone makes the eyes sad (unplugging USB doesn't).
    tcp_was_ready = (tcp_was_ready || tcp.ready()) && !settings.get("link.tcp_host").empty();
    s.host_lost = tcp_was_ready && !tcp.ready();
    s.battery = board_battery(&s.charging);
    return s;
}

// poll runs in the LVGL task every 20 ms: links, settings from the web page, internet state.
void Blinky::poll()
{
    const uint32_t now = now_ms();
    std::vector<std::string> keys;
    std::vector<core::SoundLevel::Event> heard;
    std::vector<core::Gesture> moved;
    {
        std::lock_guard lock(mu);
        keys.swap(changes);
        heard.swap(sounds);
        moved.swap(gestures);
    }
    if (!keys.empty()) {
        changed(keys, nullptr);
    }
    for (const auto ev : heard) {
        react(ev);
    }
    if (app != nullptr) {
        for (const core::Gesture gst : moved) {
            app->gesture(gst);
        }
    }
    serial_link::poll(usb, now);
    tcp_link::poll(tcp, now);
    if (ota.finished()) { // restart into the new firmware once the host has its answer
        if (ota_done_at == 0) {
            ota_done_at = now;
        } else if (now - ota_done_at > 500) {
            esp_restart();
        }
    }
    const std::string web = net::url();
    usb.set_web(web, now);
    tcp.set_web(web, now);
    router.set_device_net(net::internet());
    if (app != nullptr) {
        audio::play(app->tune());
    }

    static uint32_t last = 0, last_sensor = 0;
    if (now - last_sensor >= 60'000) { // the device's readings to the hosts, once a minute
        last_sensor = now;
        const auto temp = store.number("indoor.temp"), hum = store.number("indoor.hum"),
                   noise = store.number("indoor.noise");
        usb.send_sensor(temp, hum, noise);
        tcp.send_sensor(temp, hum, noise);
    }
    if (now - last >= 1000) {
        last = now;
        // The location found by IP address, while location.auto is on: checked only when the
        // store got something new (the lookup itself runs in the fetcher or on a host).
        static uint32_t seen = 0;
        std::vector<std::pair<std::string, std::string>> found;
        if (store.version() != seen) {
            seen = store.version();
            found = core::auto_location(settings, store);
        }
        if (!found.empty()) {
            if (auto err = settings.set_many(found)) {
                ESP_LOGW(TAG, "location not saved: %s", err->c_str());
            } else {
                ESP_LOGI(TAG, "location by IP address: %s", settings.get("location.name").c_str());
                std::vector<std::string> keys;
                for (const auto &kv : found) {
                    keys.push_back(kv.first);
                }
                changed(keys, nullptr);
            }
        }
        core::Status s = snapshot();
        std::lock_guard lock(mu);
        status = std::move(s);
    }
}

// react shows what the microphone heard on the eyes; a loud noise is reported to the hosts.
void Blinky::react(core::SoundLevel::Event ev)
{
    if (app == nullptr) {
        return;
    }
    if (ev == core::SoundLevel::Event::Loud) {
        app->mood().react("surprised", "loud noise", 1500, lv_tick_get());
        usb.send_event("sound", "loud");
        tcp.send_event("sound", "loud");
    } else if (ev == core::SoundLevel::Event::Voices) {
        app->mood().react("curious", "hears voices", 2000, lv_tick_get());
    }
}

void link_timer(lv_timer_t *)
{
    g->poll();
}

core::Status status()
{
    std::lock_guard lock(g->mu);
    return g->status;
}

} // namespace

extern "C" void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Blinky %s starting", app->version);

    if (!core::NvsStorage::init()) {
        ESP_LOGE(TAG, "NVS not available; settings will not be saved");
    }
    g = new Blinky(app);
    apply_time_zone(g->settings);

    ESP_ERROR_CHECK(board_init());

    if (g->rtc.init(board_i2c_bus())) {
        g->rtc.restore_system_time();
    }
    sensors::start_climate(board_i2c_bus(), g->store, g->settings);
    g->imu = sensors::start_motion(board_i2c_bus(), [](core::Gesture gst) {
        std::lock_guard lock(g->mu);
        g->gestures.push_back(gst);
    });

    // Host link over USB (buddy-link or the setup page, plus Improv Serial from the flasher).
    serial_link::on_bytes([](std::string_view bytes) { g->improv.feed(bytes); });
    serial_link::start();

    // WiFi, the web page and fetching on the device; then the host link over TCP.
    net::start(g->settings, g->fetcher,
               {.changed =
                    [](const std::vector<std::string> &keys) {
                        std::lock_guard lock(g->mu);
                        g->changes.insert(g->changes.end(), keys.begin(), keys.end());
                    },
                .status = status});
    tcp_link::start(g->settings);
    audio::start(g->store, g->settings, [](core::SoundLevel::Event ev) {
        std::lock_guard lock(g->mu);
        g->sounds.push_back(ev);
    });

    // The links and the router run in the LVGL task, like the UI, so results and settings
    // changes never race with drawing.
    if (board_lvgl_lock(0)) {
        board_set_rotation(std::atoi(g->settings.get("ui.rotation").c_str()));
        show_fps(g->settings);
        g->router.route();
        {
            core::Status s = g->snapshot();
            std::lock_guard lock(g->mu); // the web server may already ask for it
            g->status = std::move(s);
        }
        g->app = ui::App::create({.version = app->version,
                                .board = BOARD_NAME,
                                .seed = esp_random(),
                                .store = &g->store,
                                .settings = &g->settings,
                                .local_time = local_time,
                                .gestures = {.shake = g->imu}, // without the IMU a tap stands in for a shake
                                .status = status,
                                .backlight = [](int level) { board_set_backlight(static_cast<uint8_t>(level)); },
                                .event =
                                    [](std::string_view kind, std::string_view name, std::string_view state) {
                                        g->usb.send_event(kind, name, state);
                                        g->tcp.send_event(kind, name, state);
                                    }});
        lv_timer_create(link_timer, 20, nullptr);
        board_lvgl_unlock();
    }
    ESP_LOGI(TAG, "UI running (reset reason %d); free: %u KB internal (largest block %u KB), %u KB PSRAM",
             static_cast<int>(esp_reset_reason()), heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >> 10,
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >> 10, heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10);
    // This firmware came up: after an update, keep it (otherwise the bootloader would go
    // back to the previous one at the next reset).
    esp_ota_mark_app_valid_cancel_rollback();
}
