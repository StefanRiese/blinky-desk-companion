#include "tcp_link/tcp_link.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unistd.h>

#include "app_core/protocol.h"
#include "app_core/status.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "net/net.h"

namespace tcp_link {
namespace {

constexpr const char *kTag = "tcp_link";
constexpr size_t kQueueLen = 16;
constexpr uint32_t kConnectTimeoutMs = 5000;
constexpr uint32_t kBackoffMaxMs = 60000;
constexpr int64_t kStableUs = 10 * 1000 * 1000; // a connection that lasted this long resets the backoff
constexpr size_t kMaxPending = 16 * 1024;       // unsent output; more means the host stalled

const core::Settings *g_settings = nullptr;
TaskHandle_t g_task = nullptr;
QueueHandle_t g_lines = nullptr; // char* (malloc'd), freed by poll()
std::atomic<bool> g_connected{false};
std::atomic<bool> g_reconnect{false};
std::atomic<uint32_t> g_connections{0}; // counts connections, so poll() sees a quick reconnect
bool g_was_connected = false;           // as last seen by poll()
uint32_t g_seen_connection = 0;         // as last seen by poll()

std::mutex g_mu; // g_sock, g_pending and g_text
int g_sock = -1;
std::string g_pending; // output the socket didn't take yet; the task sends it
std::string g_text;

void set_text(std::string s)
{
    std::lock_guard lock(g_mu);
    g_text = std::move(s);
}

// wait pauses until ms pass or reconnect() is called; returns true when woken early.
bool wait(uint32_t ms)
{
    return ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms)) > 0;
}

void queue_line(std::string_view line)
{
    char *copy = static_cast<char *>(std::malloc(line.size() + 1));
    if (copy == nullptr) {
        return;
    }
    std::memcpy(copy, line.data(), line.size());
    copy[line.size()] = '\0';
    if (xQueueSend(g_lines, &copy, 0) != pdTRUE) {
        std::free(copy); // poll() is behind: drop the line
    }
}

// send_pending sends what the socket takes of g_pending without waiting. Call with g_mu
// held. Returns false when the connection is broken or the host stopped reading.
bool send_pending()
{
    while (!g_pending.empty()) {
        const int n = send(g_sock, g_pending.data(), g_pending.size(), MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }
        g_pending.erase(0, static_cast<size_t>(n));
    }
    return g_pending.size() <= kMaxPending;
}

// run reads from a connected socket until it closes or reconnect() is called, and sends the
// output that write() couldn't send at once.
void run(int s)
{
    timeval tv = {.tv_sec = 0, .tv_usec = 100 * 1000}; // also how soon pending output goes out
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    {
        std::lock_guard lock(g_mu);
        g_sock = s;
        g_pending.clear();
    }
    g_connections++;
    g_connected = true;
    core::proto::LineReader reader;
    char buf[256];
    while (!g_reconnect) {
        {
            std::lock_guard lock(g_mu);
            if (!send_pending()) {
                ESP_LOGW(kTag, "send failed or the host stopped reading: closing");
                break;
            }
        }
        const int n = recv(s, buf, sizeof buf, 0);
        if (n == 0) {
            break; // closed by the host
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        reader.feed({buf, static_cast<size_t>(n)}, queue_line);
    }
    g_connected = false;
    std::lock_guard lock(g_mu);
    g_sock = -1;
    g_pending.clear();
    close(s);
}

void task(void *)
{
    uint32_t backoff = 1000;
    for (;;) {
        g_reconnect = false;
        const std::string target = g_settings->get("link.tcp_host");
        if (target.empty()) {
            set_text("");
            wait(portMAX_DELAY);
            continue;
        }
        if (!net::has_ip()) {
            set_text("waiting for WiFi");
            wait(1000);
            continue;
        }

        std::string host, label;
        uint16_t port = kDefaultPort;
        if (target == "auto") {
            set_text("looking for buddy-link (mDNS)");
            const auto found = net::find_buddy_link(3000);
            if (!found) {
                set_text("no buddy-link found on the network");
                backoff = std::min(backoff * 2, kBackoffMaxMs);
                wait(backoff);
                continue;
            }
            host = found->addr;
            port = found->port;
            label = found->name + " at " + found->addr + ":" + std::to_string(port);
        } else if (const auto hp = core::host_port(target, kDefaultPort)) {
            host = hp->first;
            port = hp->second;
            label = host + ":" + std::to_string(port);
        } else {
            set_text("bad address \"" + target + "\"");
            wait(portMAX_DELAY); // until the setting changes
            continue;
        }

        set_text("connecting to " + label);
        std::string err;
        const int s = net::connect_tcp(host, port, kConnectTimeoutMs, err);
        if (s < 0) {
            set_text(label + ": " + err);
            wait(backoff);
            backoff = std::min(backoff * 2, kBackoffMaxMs);
            continue;
        }
        ESP_LOGI(kTag, "connected to %s", label.c_str());
        set_text(label);
        const int64_t since = esp_timer_get_time();
        run(s);
        ESP_LOGI(kTag, "connection to %s closed", label.c_str());
        set_text(label + ": connection closed");
        // A host that closes right away (wrong token, version) is retried ever more slowly.
        if (esp_timer_get_time() - since >= kStableUs) {
            backoff = 1000;
        }
        if (!g_reconnect) {
            wait(backoff);
            backoff = std::min(backoff * 2, kBackoffMaxMs);
        }
    }
}

} // namespace

void start(const core::Settings &settings)
{
    g_settings = &settings;
    g_lines = xQueueCreate(kQueueLen, sizeof(char *));
    xTaskCreatePinnedToCore(task, "tcp_link", 4096, nullptr, 5, &g_task, 0); // 4.5 KB of 6 KB stayed unused
}

// write never waits (it runs in the LVGL task): what the socket doesn't take at once stays
// in g_pending, whole lines in order, and the task sends it.
void write(std::string_view line)
{
    std::lock_guard lock(g_mu);
    if (g_sock < 0) {
        return;
    }
    g_pending.append(line);
    g_pending += '\n';
    if (!send_pending()) {
        shutdown(g_sock, SHUT_RDWR); // the task sees the connection end and reconnects
    }
}

void poll(core::HostLink &link, uint32_t now_ms)
{
    // A new connection is a new session, also when it came between two polls.
    const bool connected = g_connected;
    const uint32_t connection = g_connections;
    if (connected && connection != g_seen_connection) {
        if (g_was_connected) {
            link.down();
        }
        link.up(now_ms);
        g_seen_connection = connection; // only once seen connected: the count goes up first
    } else if (!connected && g_was_connected) {
        link.down();
    }
    g_was_connected = connected;
    char *line = nullptr;
    while (g_lines != nullptr && xQueueReceive(g_lines, &line, 0) == pdTRUE) {
        link.on_line(line, now_ms);
        std::free(line);
    }
    link.tick(now_ms);
}

void reconnect()
{
    g_reconnect = true;
    if (g_task != nullptr) {
        xTaskNotifyGive(g_task);
    }
}

std::string text()
{
    std::lock_guard lock(g_mu);
    return g_text;
}

} // namespace tcp_link
