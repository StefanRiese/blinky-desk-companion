// The host link over TCP (ESP-IDF only): the device connects to buddyd as a client, as in
// protocol/PROTOCOL.md. link.tcp_host is "" (no TCP), "host:port" (port 7531 if left out)
// or "auto" (find buddyd via mDNS, _blinky._tcp). Reconnects with a growing pause
// (1 s → 60 s) while the host can't be reached, and right away when the setting changes.
// Same shape as serial_link: a task does the I/O, poll() feeds the HostLink.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "app_core/host_link.h"
#include "app_core/settings.h"

namespace tcp_link {

constexpr uint16_t kDefaultPort = 7531;

// start starts the connection task. Call once, after net::start.
void start(const core::Settings &settings);

// write sends one line (the newline is added); dropped while not connected. Use it as the
// HostLink's send hook.
void write(std::string_view line);

// poll passes received lines to the link and runs its timers; connect and disconnect are
// reported as link up/down. Call regularly from the task that owns the link.
void poll(core::HostLink &link, uint32_t now_ms);

// reconnect closes the connection and connects again (setting changed, host silent).
void reconnect();

// text says what the connection is doing while no host is connected ("" = TCP off).
std::string text();

} // namespace tcp_link
