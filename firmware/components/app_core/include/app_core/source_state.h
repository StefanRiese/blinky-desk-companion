// The state of a configured source as the info screen, the web page and the tests see it:
// who fetches it (via) and how that goes (state). Plain types without ArduinoJson, so the UI
// can include them. Filled by the SourceRouter from the DeviceFetcher and the HostLinks.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace core {

// SourceState: pending until the first result, then ok or error; backoff while the host
// waits after errors; unavailable when nobody can fetch the source.
enum class SourceState : uint8_t { Pending, Ok, Error, Backoff, Unavailable };

// Via: who fetches a source; none when nobody can.
enum class Via : uint8_t { None, Device, Host };

constexpr const char *name(SourceState s)
{
    switch (s) {
    case SourceState::Pending:
        return "pending";
    case SourceState::Ok:
        return "ok";
    case SourceState::Error:
        return "error";
    case SourceState::Backoff:
        return "backoff";
    case SourceState::Unavailable:
        return "unavailable";
    }
    return "?";
}

constexpr const char *name(Via v)
{
    switch (v) {
    case Via::None:
        return "none";
    case Via::Device:
        return "device";
    case Via::Host:
        return "host";
    }
    return "?";
}

// host_source_state reads the state of a host's sub_state message (ok | error | backoff);
// anything else counts as an error, so its message shows.
constexpr SourceState host_source_state(std::string_view s)
{
    return s == "ok" ? SourceState::Ok : s == "backoff" ? SourceState::Backoff : SourceState::Error;
}

struct SourceStatus {
    std::string sid, src;
    Via via = Via::None;
    std::string link; // the HostLink's name ("usb", "tcp") when via is Host
    SourceState state = SourceState::Pending;
    std::string msg;
    int every = 0;
};

} // namespace core
