// The host link over the native USB port (USB-Serial-JTAG): buddy-link, or the setup page in
// the browser (Web Serial). The same port carries the ESP log; while a host is connected,
// log lines are sent as {"t":"log"} messages so they don't break the NDJSON stream (the host
// ignores the plain lines it sees before that, as in protocol/PROTOCOL.md).
#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "app_core/host_link.h"

namespace serial_link {

// start installs the USB driver, routes the ESP log through the port and starts the reader
// task. Call once, before the HostLink is used.
void start();

// write sends one line (the newline is added). Thread-safe; dropped when no USB host is
// attached. Use it as the HostLink's send hook.
void write(std::string_view line);

// write_bytes sends bytes as they are (Improv Serial packets). Thread-safe.
void write_bytes(std::string_view bytes);

// on_bytes sets a handler that sees every received chunk, before the NDJSON framing (it
// runs in the reader task). Used for Improv Serial. Call before start().
void on_bytes(std::function<void(std::string_view bytes)> handler);

// poll passes received lines to the link and runs its timers; it also reports USB plug and
// unplug as link up/down. Call regularly (every 20–50 ms) from the task that owns the link.
void poll(core::HostLink &link, uint32_t now_ms);

} // namespace serial_link
