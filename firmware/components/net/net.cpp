// WiFi station and setup access point, captive portal DNS, internet check and the network
// task (DeviceFetcher, AP timeout, Improv connect timeout).
#include "net/net.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string_view>
#include <unistd.h>

#include "app_core/captive_dns.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "net_internal.h"

namespace net {
namespace {

constexpr const char *kTag = "wifi";
constexpr uint32_t kBootWaitMs = 10000;      // stored network not reachable: open the setup AP
constexpr uint32_t kApMs = 3 * 60 * 1000;    // the setup AP stays this long
constexpr uint32_t kApExtendMs = 60 * 1000;  // … and longer while someone is connected to it
constexpr uint32_t kConnectWaitMs = 20000;   // Improv: give up on new credentials after this
constexpr uint32_t kCheckOnlineMs = 10 * 60 * 1000;
constexpr uint32_t kCheckOfflineMs = 60 * 1000;
constexpr uint32_t kRetryMaxMs = 30000;

struct Net {
    core::Settings *settings = nullptr;
    core::DeviceFetcher *fetcher = nullptr;
    Hooks hooks;
    esp_netif_t *sta = nullptr;
    TaskHandle_t task = nullptr;

    std::mutex mu; // everything below
    std::string ssid;
    std::string ip;
    bool ap_on = false;
    uint32_t ap_until = 0;
    uint32_t retry_ms = 1000;
    uint32_t retry_at = 0;
    bool retry_due = false;
    uint32_t check_at = 0;
    bool check_due = false;
    std::function<void(bool)> improv_done;
    uint32_t improv_until = 0;
};
Net g;
std::atomic<bool> g_internet{false};
std::atomic<bool> g_has_ip{false};

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

bool reached(uint32_t now, uint32_t at)
{
    return static_cast<int32_t>(now - at) >= 0;
}

// copy_field puts text into a fixed WiFi config field (SSID, password). The field may be
// filled completely without a terminating zero: the driver takes 32-byte SSIDs and 64-byte
// passwords that way.
template <size_t N> void copy_field(uint8_t (&field)[N], std::string_view text)
{
    std::memset(field, 0, N);
    std::memcpy(field, text.data(), std::min(N, text.size()));
}

void wake()
{
    if (g.task != nullptr) {
        xTaskNotifyGive(g.task);
    }
}

// finish_improv reports the outcome of an Improv connect (outside the lock).
void finish_improv(bool ok)
{
    std::function<void(bool)> done;
    {
        std::lock_guard lock(g.mu);
        done = std::move(g.improv_done);
        g.improv_done = nullptr;
    }
    if (done) {
        done(ok);
    }
}

void dns_task(void *)
{
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (s < 0 || bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) {
        ESP_LOGE(kTag, "captive DNS: cannot open port 53");
        if (s >= 0) {
            close(s);
        }
        vTaskDelete(nullptr);
        return;
    }
    timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char buf[512];
    for (;;) {
        {
            std::lock_guard lock(g.mu);
            if (!g.ap_on) {
                break;
            }
        }
        sockaddr_in from = {};
        socklen_t len = sizeof from;
        const int n = recvfrom(s, buf, sizeof buf, 0, reinterpret_cast<sockaddr *>(&from), &len);
        if (n <= 0) {
            continue;
        }
        const std::string reply = core::captive_dns_reply({buf, static_cast<size_t>(n)}, {192, 168, 4, 1});
        if (!reply.empty()) {
            sendto(s, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr *>(&from), len);
        }
    }
    close(s);
    vTaskDelete(nullptr);
}

void start_ap()
{
    {
        std::lock_guard lock(g.mu);
        g.ap_until = now_ms() + kApMs;
        if (g.ap_on) {
            return;
        }
        g.ap_on = true;
    }
    wifi_config_t ap = {};
    copy_field(ap.ap.ssid, kSetupSsid);
    ap.ap.ssid_len = static_cast<uint8_t>(std::strlen(kSetupSsid));
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    xTaskCreate(dns_task, "captive_dns", 3072, nullptr, 3, nullptr);
    ESP_LOGI(kTag, "setup access point \"%s\" open (http://192.168.4.1/)", kSetupSsid);
}

void stop_ap()
{
    {
        std::lock_guard lock(g.mu);
        if (!g.ap_on) {
            return;
        }
        g.ap_on = false; // the DNS task ends within a second
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(kTag, "setup access point closed");
}

// apply_station configures the station from the settings and connects (or disconnects).
void apply_station()
{
    const std::string ssid = g.settings->get("wifi.ssid"), pass = g.settings->get("wifi.pass");
    {
        std::lock_guard lock(g.mu);
        g.ssid = ssid;
        g.retry_ms = 1000;
        g.retry_due = false;
    }
    esp_wifi_disconnect();
    if (ssid.empty()) {
        return;
    }
    wifi_config_t sta = {};
    copy_field(sta.sta.ssid, ssid);
    copy_field(sta.sta.password, pass);
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN; // whatever the network uses
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    ESP_LOGI(kTag, "connecting to \"%s\"", ssid.c_str());
    esp_wifi_connect();
}

void on_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto *e = static_cast<wifi_event_sta_disconnected_t *>(data);
        const bool had_ip = g_has_ip.exchange(false);
        g_internet = false;
        {
            std::lock_guard lock(g.mu);
            g.ip.clear();
            if (g.ssid.empty()) {
                return; // disconnected on purpose
            }
            // Retry with a growing pause (slower while the setup AP is on: scanning for the
            // network moves the AP's channel and disturbs whoever is connected to it).
            g.retry_at = now_ms() + (g.ap_on ? kRetryMaxMs : g.retry_ms);
            g.retry_due = true;
            g.retry_ms = std::min(g.retry_ms * 2, kRetryMaxMs);
        }
        if (had_ip) {
            ESP_LOGW(kTag, "WiFi lost (reason %d)", e->reason);
        }
        wake();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *e = static_cast<ip_event_got_ip_t *>(data);
        char ip[16];
        esp_ip4addr_ntoa(&e->ip_info.ip, ip, sizeof ip);
        {
            std::lock_guard lock(g.mu);
            g.ip = ip;
            g.retry_ms = 1000;
            g.check_due = true; // is there internet behind it?
            g.check_at = now_ms();
        }
        g_has_ip = true;
        ESP_LOGI(kTag, "connected, address %s", ip);
        wake();
    }
}

// check_internet: can we reach Open-Meteo (DNS and a TCP connection to port 443)?
bool check_internet()
{
    std::string err;
    const int s = connect_tcp("api.open-meteo.com", 443, 5000, err);
    if (s < 0) {
        ESP_LOGW(kTag, "no internet: %s", err.c_str());
        return false;
    }
    close(s);
    return true;
}

void net_task(void *)
{
    const uint32_t boot = now_ms();
    bool boot_checked = false;
    for (;;) {
        const uint32_t now = now_ms();
        bool retry = false, check = false, ap_close = false, improv_timeout = false, no_ssid = false;
        {
            std::lock_guard lock(g.mu);
            no_ssid = g.ssid.empty();
            if (g.retry_due && reached(now, g.retry_at)) {
                g.retry_due = false;
                retry = true;
            }
            if (g.check_due && reached(now, g.check_at)) {
                g.check_due = false;
                check = true;
            }
            ap_close = g.ap_on && reached(now, g.ap_until);
            improv_timeout = g.improv_done && reached(now, g.improv_until);
        }
        if (!boot_checked && (no_ssid || reached(now, boot + kBootWaitMs))) {
            boot_checked = true;
            if (!g_has_ip) {
                start_ap();
            }
        }
        if (retry) {
            esp_wifi_connect();
        }
        if (check && g_has_ip) {
            const bool online = check_internet();
            g_internet = online && g_has_ip;
            std::lock_guard lock(g.mu);
            g.check_at = now_ms() + (online ? kCheckOnlineMs : kCheckOfflineMs);
            g.check_due = true;
        }
        if (ap_close) {
            wifi_sta_list_t list = {};
            if (esp_wifi_ap_get_sta_list(&list) == ESP_OK && list.num > 0) {
                std::lock_guard lock(g.mu);
                g.ap_until = now + kApExtendMs;
            } else {
                stop_ap();
            }
        }
        if (improv_timeout) {
            finish_improv(false);
        } else if (g_has_ip) {
            bool waiting = false;
            {
                std::lock_guard lock(g.mu);
                waiting = static_cast<bool>(g.improv_done);
            }
            if (waiting) {
                finish_improv(true);
            }
        }

        uint32_t wait = 1000;
        if (g_internet) {
            wait = std::min(wait, g.fetcher->run_due());
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(std::max<uint32_t>(wait, 10)));
    }
}

} // namespace

// ---------------------------------------------------------------- internal

core::Settings &settings()
{
    return *g.settings;
}

const Hooks &hooks()
{
    return g.hooks;
}

bool ap_on()
{
    std::lock_guard lock(g.mu);
    return g.ap_on;
}

// ---------------------------------------------------------------- public

void start(core::Settings &settings, core::DeviceFetcher &fetcher, Hooks hooks)
{
    g.settings = &settings;
    g.fetcher = &fetcher;
    g.hooks = std::move(hooks);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    g.sta = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(g.sta, kHostname);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_storage(WIFI_STORAGE_RAM); // the credentials live in the settings
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr, nullptr);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(kHostname);
        mdns_instance_name_set("Blinky");
        mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    } else {
        ESP_LOGW(kTag, "mDNS not available");
    }
    start_web();
    apply_station();
    xTaskCreatePinnedToCore(net_task, "net", 8192, nullptr, 4, &g.task, 0);
}

State state()
{
    std::lock_guard lock(g.mu);
    using Wifi = core::Status::Wifi;
    State s{.wifi = Wifi::Off, .text = "", .ip = g.ip};
    if (!g.ip.empty()) {
        s.wifi = g_internet ? Wifi::Online : Wifi::NoInternet;
        s.text = g.ssid + "  ·  " + (g_internet ? g.ip : g.ip + ", no internet");
    } else if (g.ap_on) {
        s.wifi = Wifi::SetupAp;
        s.text = std::string("setup: join WiFi ") + kSetupSsid;
    } else if (!g.ssid.empty()) {
        s.wifi = Wifi::Connecting;
        s.text = "connecting to " + g.ssid;
    }
    return s;
}

bool internet()
{
    return g_internet;
}

bool has_ip()
{
    return g_has_ip;
}

std::string url()
{
    std::lock_guard lock(g.mu);
    return g.ip.empty() ? std::string() : "http://" + g.ip + "/";
}

void reconnect()
{
    apply_station();
}

void connect(const std::string &ssid, const std::string &pass, std::function<void(bool ok)> done)
{
    if (auto err = g.settings->set_many({{"wifi.ssid", ssid}, {"wifi.pass", pass}})) {
        ESP_LOGW(kTag, "Improv: %s", err->c_str());
        done(false);
        return;
    }
    {
        std::lock_guard lock(g.mu);
        g.improv_done = std::move(done);
        g.improv_until = now_ms() + kConnectWaitMs;
    }
    g_has_ip = false; // wait for the new network's address
    g_internet = false;
    apply_station();
    wake();
}

std::vector<core::Improv::Network> scan()
{
    wifi_scan_config_t sc = {};
    std::vector<core::Improv::Network> out;
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
        return out;
    }
    uint16_t n = 20;
    wifi_ap_record_t recs[20];
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        return out;
    }
    for (uint16_t i = 0; i < n; i++) {
        const std::string ssid(reinterpret_cast<const char *>(recs[i].ssid));
        const bool dup = std::any_of(out.begin(), out.end(), [&](const auto &o) { return o.ssid == ssid; });
        if (!ssid.empty() && !dup) { // strongest first; one entry per name
            out.push_back({.ssid = ssid, .rssi = recs[i].rssi, .secured = recs[i].authmode != WIFI_AUTH_OPEN});
        }
    }
    return out;
}

} // namespace net
