// What the info screen and the status bar show about WiFi, the host links and the sources.
// The firmware fills it from the WiFi service, the HostLinks and the SourceRouter; the
// simulator and the tests fill it by hand.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app_core/host_link.h"
#include "app_core/source_router.h"

namespace core {

struct Status {
    enum class Wifi { Off, Connecting, Online, NoInternet, SetupAp };
    struct Link {
        bool up = false;  // a host or the setup page is connected
        std::string text; // host name, or what the link is doing
    };

    Wifi wifi = Wifi::Off;
    std::string wifi_text; // "Home · 192.168.1.23", "connecting to Home", "Blinky-Setup"
    Link usb, tcp;
    std::vector<SourceRouter::Status> sources;
};

// describe says what a link is doing: the host's name, "setup page", "waiting for buddyd",
// or the host's error (wrong token); "" while the transport is down.
std::string describe(const HostLink &link);

// host_port splits "host:port" (port optional, default def); nullopt for a bad port or host.
std::optional<std::pair<std::string, uint16_t>> host_port(std::string_view s, uint16_t def);

} // namespace core
