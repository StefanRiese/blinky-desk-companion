#include "serial_link/serial_link.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app_core/log_lines.h"
#include "app_core/protocol.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace serial_link {
namespace {

constexpr size_t kQueueLen = 16;           // received lines waiting for poll()
constexpr TickType_t kWriteWait = pdMS_TO_TICKS(50);
constexpr TickType_t kLogFlushAfter = pdMS_TO_TICKS(100); // an unfinished log line goes out after this

QueueHandle_t g_lines = nullptr; // char* (malloc'd), freed by poll()
SemaphoreHandle_t g_tx = nullptr; // recursive: a log from inside a write must not deadlock
int g_tx_depth = 0;               // > 0 while a write is in progress (guarded by g_tx)
std::atomic<bool> g_json_logs{false};
bool g_usb_connected = false; // as last seen by poll()
std::function<void(std::string_view)> g_on_bytes;

// Log output collected up to the newline (some parts print a line in two calls).
SemaphoreHandle_t g_log_mu = nullptr;
core::LogLines g_log_lines;           // guarded by g_log_mu
TaskHandle_t g_log_owner = nullptr;   // the task whose line is unfinished
TickType_t g_log_since = 0;           // when that task last added to it

// write_raw writes bytes as one piece; nested writes (a log line from inside the USB
// driver) are dropped.
void write_raw(std::string_view a, std::string_view b = {})
{
    if (g_tx == nullptr || !usb_serial_jtag_is_connected()) {
        return;
    }
    xSemaphoreTakeRecursive(g_tx, portMAX_DELAY);
    if (g_tx_depth == 0) {
        g_tx_depth++;
        usb_serial_jtag_write_bytes(a.data(), a.size(), kWriteWait);
        if (!b.empty()) {
            usb_serial_jtag_write_bytes(b.data(), b.size(), kWriteWait);
        }
        g_tx_depth--;
    }
    xSemaphoreGiveRecursive(g_tx);
}

// send_log_line sends one finished log line: as {"t":"log",...} while a host is connected,
// else as plain text.
void send_log_line(std::string_view line)
{
    if (!g_json_logs) {
        write_raw(line, "\n");
        return;
    }
    const auto l = core::parse_log_line(line);
    if (l) {
        write_raw(core::proto::log(l->level, l->tag, l->msg), "\n");
    }
}

// collect_log adds output to the unfinished line and returns the lines it finished. An
// unfinished line of another task, or one older than kLogFlushAfter, goes out as it is first.
// The lines are sent after the lock is released, because the USB driver may log too.
std::vector<std::string> collect_log(std::string_view text, bool end_line)
{
    std::vector<std::string> done;
    const auto keep = [&](std::string_view line) { done.emplace_back(line); };
    const TaskHandle_t me = xTaskGetCurrentTaskHandle();
    const TickType_t now = xTaskGetTickCount();
    xSemaphoreTake(g_log_mu, portMAX_DELAY);
    if (g_log_lines.pending() && (g_log_owner != me || now - g_log_since > kLogFlushAfter)) {
        g_log_lines.flush(keep);
    }
    g_log_lines.feed(text, keep);
    if (end_line) {
        g_log_lines.flush(keep);
    }
    g_log_owner = me;
    g_log_since = now;
    xSemaphoreGive(g_log_mu);
    return done;
}

// log_vprintf replaces the ESP log output. Lines look like "I (1234) tag: message"; while a
// host is connected they are sent as {"t":"log","lvl":"I","tag":"tag","msg":"message"}.
// Output is collected up to the newline: the WiFi driver prints "I (5758) wifi:" and the
// text in two calls.
int log_vprintf(const char *fmt, va_list args)
{
    char buf[256];
    const int n = vsnprintf(buf, sizeof buf, fmt, args);
    if (n <= 0) {
        return n;
    }
    const bool cut = static_cast<size_t>(n) >= sizeof buf; // the newline was cut off too
    const std::string_view s(buf, std::min<size_t>(static_cast<size_t>(n), sizeof buf - 1));
    if (g_log_mu == nullptr) {
        write_raw(s);
        return n;
    }
    for (const std::string &line : collect_log(s, cut)) {
        send_log_line(line);
    }
    return n;
}

// flush_stale_log sends an unfinished log line nobody has added to for kLogFlushAfter.
void flush_stale_log()
{
    if (g_log_mu == nullptr) {
        return;
    }
    std::vector<std::string> done;
    xSemaphoreTake(g_log_mu, portMAX_DELAY);
    if (g_log_lines.pending() && xTaskGetTickCount() - g_log_since > kLogFlushAfter) {
        g_log_lines.flush([&](std::string_view line) { done.emplace_back(line); });
    }
    xSemaphoreGive(g_log_mu);
    for (const std::string &line : done) {
        send_log_line(line);
    }
}

void reader_task(void *)
{
    core::proto::LineReader reader;
    char buf[128];
    for (;;) {
        const int n = usb_serial_jtag_read_bytes(buf, sizeof buf, portMAX_DELAY);
        if (n <= 0) {
            continue;
        }
        if (g_on_bytes) {
            g_on_bytes({buf, static_cast<size_t>(n)});
        }
        reader.feed({buf, static_cast<size_t>(n)}, [](std::string_view line) {
            char *copy = static_cast<char *>(std::malloc(line.size() + 1));
            if (copy == nullptr) {
                return;
            }
            std::memcpy(copy, line.data(), line.size());
            copy[line.size()] = '\0';
            if (xQueueSend(g_lines, &copy, 0) != pdTRUE) {
                std::free(copy); // poll() is behind: drop the line, like a full serial buffer
            }
        });
    }
}

} // namespace

void start()
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 4096;
    cfg.tx_buffer_size = 4096;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    usb_serial_jtag_vfs_use_driver(); // printf goes through the driver as well

    g_tx = xSemaphoreCreateRecursiveMutex();
    g_log_mu = xSemaphoreCreateMutex();
    g_lines = xQueueCreate(kQueueLen, sizeof(char *));
    esp_log_set_vprintf(log_vprintf);
    xTaskCreate(reader_task, "usb_link", 4096, nullptr, 5, nullptr);
}

void write(std::string_view line)
{
    write_raw(line, "\n");
}

void write_bytes(std::string_view bytes)
{
    write_raw(bytes);
}

void on_bytes(std::function<void(std::string_view bytes)> handler)
{
    g_on_bytes = std::move(handler);
}

void poll(core::HostLink &link, uint32_t now_ms)
{
    const bool connected = usb_serial_jtag_is_connected();
    if (connected != g_usb_connected) {
        g_usb_connected = connected;
        if (connected) {
            link.up(now_ms); // a computer is attached: say hello
        } else {
            link.down();
        }
    }
    char *line = nullptr;
    while (g_lines != nullptr && xQueueReceive(g_lines, &line, 0) == pdTRUE) {
        link.on_line(line, now_ms);
        std::free(line);
    }
    link.tick(now_ms);
    g_json_logs = link.ready();
    flush_stale_log();
}

} // namespace serial_link
