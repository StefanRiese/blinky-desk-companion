// Networking on the device (ESP-IDF only):
// - WiFi station with the stored network (wifi.ssid / wifi.pass), reconnecting by itself
// - the setup access point "Blinky-Setup" with a captive portal, when no network is set or
//   the stored one isn't reachable within 10 s after boot; it closes after 3 minutes
//   (later while a phone or laptop is still connected to it)
// - the web config page on port 80 (http://blinky.local/ or the IP): all settings, from the
//   registry, and the link and source state; mDNS name "blinky"
// - an internet check, NTP, HTTPS requests, and the network task that runs the
//   DeviceFetcher while the internet is reachable
// - Improv Serial support: WiFi scan, and connect with new credentials
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app_core/device_fetcher.h"
#include "app_core/improv.h"
#include "app_core/settings.h"
#include "app_core/status.h"

namespace net {

inline constexpr const char *kHostname = "blinky";
inline constexpr const char *kSetupSsid = "Blinky-Setup";

struct Hooks {
    // changed: the web page saved settings (runs in the web server's task).
    std::function<void(const std::vector<std::string> &keys)> changed;
    // status: links and sources for the web page (called from the web server's task, so
    // it must be thread-safe).
    std::function<core::Status()> status;
};

// start brings up WiFi, the web page, mDNS and the network task. Call once, after the
// settings are loaded; fetcher runs in the network task. With wifi.enabled = false it does
// nothing (USB only); changing that setting takes a restart.
void start(core::Settings &settings, core::DeviceFetcher &fetcher, Hooks hooks);

struct State {
    core::Status::Wifi wifi = core::Status::Wifi::Off;
    std::string text; // for the info screen: "Home  ·  192.168.1.23", "connecting to Home", …
    std::string ip;   // "" without an address
};
State state();
// internet: the station has an address and the internet check succeeded.
bool internet();
bool has_ip();
// url is the web page's address on the WiFi ("http://192.168.1.23/"), or "".
std::string url();

// reconnect applies changed WiFi settings.
void reconnect();
// connect stores new credentials and connects; done(ok) follows within 20 s (Improv).
void connect(const std::string &ssid, const std::string &pass, std::function<void(bool ok)> done);
// scan lists the networks in reach (blocks for about 2 s).
std::vector<core::Improv::Network> scan();

// http_get fetches a URL (HTTPS with the certificate bundle); the body is limited to
// core::fetch::kMaxBody. Blocking; used as the DeviceFetcher's get hook.
core::fetch::Response http_get(const std::string &url, const core::fetch::Headers &headers);
// sync_time asks pool.ntp.org for the time (blocks up to 10 s).
std::optional<int64_t> sync_time();

// connect_tcp opens a TCP connection (DNS names or IPv4), or returns -1 with err set.
int connect_tcp(const std::string &host, uint16_t port, uint32_t timeout_ms, std::string &err);

// find_buddy_link looks for buddy-link on the network (mDNS _blinky._tcp, proto=1).
struct Found {
    std::string name, addr;
    uint16_t port = 0;
};
std::optional<Found> find_buddy_link(uint32_t timeout_ms);

} // namespace net
