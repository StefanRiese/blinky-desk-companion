#include "app_core/status.h"

#include <charconv>

namespace core {

std::string describe(const HostLink &link)
{
    switch (link.state()) {
    case HostLink::State::Off:
        return {};
    case HostLink::State::Handshake:
        return link.error().empty() ? "waiting for buddy-link" : link.error();
    case HostLink::State::Ready:
        if (link.peer() == HostLink::Peer::Setup) {
            return "setup page";
        }
        return link.host_name().empty() ? "buddy-link" : link.host_name();
    }
    return {};
}

std::optional<std::pair<std::string, uint16_t>> host_port(std::string_view s, uint16_t def)
{
    const size_t colon = s.rfind(':');
    std::string_view host = s.substr(0, colon);
    uint16_t port = def;
    if (colon != std::string_view::npos) {
        const std::string_view p = s.substr(colon + 1);
        unsigned v = 0;
        const auto [end, ec] = std::from_chars(p.data(), p.data() + p.size(), v);
        if (p.empty() || ec != std::errc() || end != p.data() + p.size() || v == 0 || v > 65535) {
            return std::nullopt;
        }
        port = static_cast<uint16_t>(v);
    }
    if (host.empty() || host.find_first_of(" /:") != std::string_view::npos) {
        return std::nullopt;
    }
    return std::make_pair(std::string(host), port);
}

} // namespace core
