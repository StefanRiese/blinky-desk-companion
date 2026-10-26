#include "serial_link/serial_link.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

QueueHandle_t g_lines = nullptr; // char* (malloc'd), freed by poll()
SemaphoreHandle_t g_tx = nullptr; // recursive: a log from inside a write must not deadlock
int g_tx_depth = 0;               // > 0 while a write is in progress (guarded by g_tx)
std::atomic<bool> g_json_logs{false};
bool g_usb_connected = false; // as last seen by poll()

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

// log_vprintf replaces the ESP log output. Lines look like "I (1234) tag: message"; while a
// host is connected they are sent as {"t":"log","lvl":"I","tag":"tag","msg":"message"}.
int log_vprintf(const char *fmt, va_list args)
{
    char buf[256];
    const int n = vsnprintf(buf, sizeof buf, fmt, args);
    if (n <= 0) {
        return n;
    }
    std::string_view s(buf, std::min<size_t>(static_cast<size_t>(n), sizeof buf - 1));
    if (!g_json_logs) {
        write_raw(s);
        return n;
    }
    // Strip colours and the newline.
    while (s.size() > 1 && s[0] == '\033') {
        const size_t m = s.find('m');
        s = m == std::string_view::npos ? std::string_view{} : s.substr(m + 1);
    }
    if (const size_t esc = s.find('\033'); esc != std::string_view::npos) {
        s = s.substr(0, esc);
    }
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    if (s.empty()) {
        return n;
    }
    char level = 'I';
    std::string_view tag, msg = s;
    if (s.size() > 4 && std::strchr("EWIDV", s[0]) != nullptr && s[1] == ' ' && s[2] == '(') {
        level = s[0];
        const size_t close = s.find(") ");
        const size_t colon = close == std::string_view::npos ? close : s.find(": ", close + 2);
        if (colon != std::string_view::npos) {
            tag = s.substr(close + 2, colon - close - 2);
            msg = s.substr(colon + 2);
        }
    }
    const std::string line = core::proto::log(level, tag, msg);
    write_raw(line, "\n");
    return n;
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
    g_lines = xQueueCreate(kQueueLen, sizeof(char *));
    esp_log_set_vprintf(log_vprintf);
    xTaskCreate(reader_task, "usb_link", 4096, nullptr, 5, nullptr);
}

void write(std::string_view line)
{
    write_raw(line, "\n");
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
}

} // namespace serial_link
