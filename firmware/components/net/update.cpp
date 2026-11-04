// Firmware updates over WiFi: the core::Updater with its device hooks. It reads the start of
// the board's app image on GitHub to learn its version, and installs by streaming the image
// into the other app partition (esp_ota_*). Runs in the network task (net.cpp).
#include <ctime>
#include <memory>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net_internal.h"

namespace net {
namespace {

constexpr const char *kTag = "update";
constexpr int kMaxRedirects = 5;
constexpr size_t kChunk = 4096;

std::unique_ptr<core::Updater> g_updater;

// stream GETs url (following redirects; GitHub sends release files on from a long signed
// address) and hands the body to data() piece by piece until it returns false. start() gets
// the length (-1 if unknown) before the first piece and may refuse with an error.
std::optional<std::string> stream(const std::string &url, const char *range,
                                  const std::function<std::optional<std::string>(int64_t len)> &start,
                                  const std::function<bool(const char *data, size_t len)> &data)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 15000;
    cfg.user_agent = "Blinky";
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 4096; // the signed download address is long
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (c == nullptr) {
        return "out of memory";
    }
    if (range != nullptr) {
        esp_http_client_set_header(c, "Range", range);
    }
    std::optional<std::string> err;
    for (int redirects = 0;; redirects++) {
        if (esp_err_t e = esp_http_client_open(c, 0); e != ESP_OK) {
            err = std::string("request failed (") + esp_err_to_name(e) + ")";
            break;
        }
        const int64_t len = esp_http_client_fetch_headers(c);
        const int status = esp_http_client_get_status_code(c);
        if ((status == 301 || status == 302 || status == 307 || status == 308) && redirects < kMaxRedirects) {
            esp_http_client_set_redirection(c);
            esp_http_client_close(c);
            continue;
        }
        if (status != 200 && status != 206) {
            err = status == 404 ? "no firmware for this board in the release" : "HTTP " + std::to_string(status);
            esp_http_client_close(c);
            break;
        }
        if ((err = start(len))) {
            esp_http_client_close(c);
            break;
        }
        std::unique_ptr<char[]> buf(new char[kChunk]);
        for (;;) {
            const int n = esp_http_client_read(c, buf.get(), kChunk);
            if (n < 0) {
                err = "reading the download failed";
                break;
            }
            if (n == 0 || !data(buf.get(), static_cast<size_t>(n))) {
                break;
            }
        }
        esp_http_client_close(c);
        break;
    }
    esp_http_client_cleanup(c);
    return err;
}

std::optional<std::string> head(const std::string &url, std::string &err)
{
    std::string out;
    const auto e = stream(
        url, "bytes=0-255", [](int64_t) { return std::nullopt; },
        [&out](const char *d, size_t n) {
            out.append(d, n);
            return out.size() < core::Updater::kHeadBytes;
        });
    if (e) {
        err = *e;
        return std::nullopt;
    }
    return out;
}

std::optional<std::string> install(const std::string &url, const std::function<void(int)> &progress)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
    if (part == nullptr) {
        return "no partition for updates";
    }
    esp_ota_handle_t ota = 0;
    bool begun = false;
    int64_t size = 0, got = 0;
    std::optional<std::string> write_err;
    auto failed = [](const char *what, esp_err_t e) { return std::string(what) + ": " + esp_err_to_name(e); };
    ESP_LOGI(kTag, "downloading %s", url.c_str());
    auto err = stream(
        url, nullptr,
        [&](int64_t len) -> std::optional<std::string> {
            if (len <= 0) {
                return "the download has no length";
            }
            if (static_cast<uint64_t>(len) > part->size) {
                return "the image is larger than the app partition";
            }
            // Sequential writes erase each sector just before it is written.
            if (esp_err_t e = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota); e != ESP_OK) {
                return failed("esp_ota_begin", e);
            }
            begun = true;
            size = len;
            ESP_LOGI(kTag, "%lld bytes to %s", static_cast<long long>(len), part->label);
            return std::nullopt;
        },
        [&](const char *d, size_t n) {
            if (esp_err_t e = esp_ota_write(ota, d, n); e != ESP_OK) {
                write_err = failed("esp_ota_write", e);
                return false;
            }
            const int before = static_cast<int>(got * 100 / size);
            got += static_cast<int64_t>(n);
            const int now = static_cast<int>(got * 100 / size);
            if (now != before) {
                progress(now);
            }
            return true;
        });
    if (!err && write_err) {
        err = write_err;
    }
    if (!err && got != size) {
        err = "the download stopped at " + std::to_string(got * 100 / size) + " %";
    }
    if (err) {
        if (begun) {
            esp_ota_abort(ota);
        }
        ESP_LOGW(kTag, "update failed: %s", err->c_str());
        return err;
    }
    if (esp_err_t e = esp_ota_end(ota); e != ESP_OK) {
        return failed("the image is not valid", e);
    }
    if (esp_err_t e = esp_ota_set_boot_partition(part); e != ESP_OK) {
        return failed("esp_ota_set_boot_partition", e);
    }
    ESP_LOGI(kTag, "ready, %s boots next", part->label);
    return std::nullopt;
}

int local_hour()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm = {};
    localtime_r(&t, &tm);
    return tm.tm_year + 1900 < 2025 ? -1 : tm.tm_hour; // -1 before the clock is set
}

} // namespace

void start_updater()
{
    g_updater = std::make_unique<core::Updater>(
        settings(), hooks().fw, hooks().board,
        core::Updater::Hooks{.head = head,
                             .install = install,
                             .busy = [] { return hooks().ota_busy && hooks().ota_busy(); },
                             .local_hour = local_hour,
                             .restart =
                                 [] {
                                     ESP_LOGI(kTag, "restarting into the new firmware");
                                     vTaskDelay(pdMS_TO_TICKS(1000)); // the web page sees "installed"
                                     esp_restart();
                                 }});
}

uint32_t run_updater(uint32_t now)
{
    return g_updater ? g_updater->run_due(now) : 60000;
}

core::Updater::Info update_info()
{
    return g_updater ? g_updater->info() : core::Updater::Info{};
}

void update_check()
{
    if (g_updater) {
        g_updater->check();
    }
}

bool update_install(std::string &err)
{
    if (!g_updater) {
        err = "WiFi is off";
        return false;
    }
    return g_updater->install(err);
}

} // namespace net
