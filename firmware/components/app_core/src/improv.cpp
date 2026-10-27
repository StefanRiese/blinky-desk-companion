#include "app_core/improv.h"

#include <algorithm>

namespace core {
namespace {

constexpr std::string_view kHeader = "IMPROV";
constexpr uint8_t kVersion = 1;
constexpr uint8_t kTypeState = 0x01, kTypeError = 0x02, kTypeRpc = 0x03, kTypeRpcResult = 0x04;

} // namespace

std::string Improv::packet(uint8_t type, std::string_view data)
{
    std::string p(kHeader);
    p += static_cast<char>(kVersion);
    p += static_cast<char>(type);
    p += static_cast<char>(data.size());
    p += data;
    uint8_t sum = 0;
    for (char c : p) {
        sum += static_cast<uint8_t>(c);
    }
    p += static_cast<char>(sum);
    p += '\n';
    return p;
}

std::string Improv::rpc_result(uint8_t command, const std::vector<std::string> &strings)
{
    std::string data;
    for (const auto &s : strings) {
        data += static_cast<char>(std::min<size_t>(s.size(), 255));
        data += s.substr(0, 255);
    }
    std::string out;
    out += static_cast<char>(command);
    out += static_cast<char>(data.size());
    out += data;
    return packet(kTypeRpcResult, out);
}

void Improv::feed(std::string_view bytes)
{
    std::lock_guard lock(mu_);
    buf_ += bytes;
    for (;;) {
        // Drop everything before the next header (log lines, JSON for the host link).
        const size_t start = buf_.find(kHeader);
        if (start == std::string::npos) {
            // Keep a tail that might be the start of a header.
            buf_.erase(0, buf_.size() > kHeader.size() ? buf_.size() - kHeader.size() : 0);
            return;
        }
        buf_.erase(0, start);
        if (buf_.size() < 9) {
            return;
        }
        const auto version = static_cast<uint8_t>(buf_[6]);
        const auto type = static_cast<uint8_t>(buf_[7]);
        const auto len = static_cast<uint8_t>(buf_[8]);
        if (buf_.size() < 9u + len + 1u) {
            return;
        }
        uint8_t sum = 0;
        for (size_t i = 0; i < 9u + len; i++) {
            sum += static_cast<uint8_t>(buf_[i]);
        }
        const bool ok = version == kVersion && sum == static_cast<uint8_t>(buf_[9 + len]);
        const std::string data = buf_.substr(9, len);
        buf_.erase(0, ok ? 10u + len : 1u); // a bad packet: look for the next header
        if (!ok) {
            if (version == kVersion) {
                send_error(Error::InvalidRpc);
            }
            continue;
        }
        handle(type, data);
    }
}

void Improv::handle(uint8_t type, std::string_view data)
{
    if (type != kTypeRpc) {
        return;
    }
    if (data.size() < 2 || static_cast<uint8_t>(data[1]) != data.size() - 2) {
        send_error(Error::InvalidRpc);
        return;
    }
    const auto command = static_cast<uint8_t>(data[0]);
    const std::string_view args = data.substr(2);
    const std::string url = hooks_.url ? hooks_.url() : std::string();

    switch (command) {
    case kWifiSettings: {
        if (args.empty() || args.size() < 1u + static_cast<uint8_t>(args[0]) + 1u) {
            send_error(Error::InvalidRpc);
            return;
        }
        const size_t ssid_len = static_cast<uint8_t>(args[0]);
        const std::string ssid(args.substr(1, ssid_len));
        const size_t pass_len = static_cast<uint8_t>(args[1 + ssid_len]);
        if (args.size() < 2 + ssid_len + pass_len || ssid.empty()) {
            send_error(Error::InvalidRpc);
            return;
        }
        const std::string pass(args.substr(2 + ssid_len, pass_len));
        send_error(Error::None);
        send_state(State::Provisioning);
        provisioning_ = true;
        if (hooks_.connect) {
            hooks_.connect(ssid, pass);
        }
        return;
    }
    case kCurrentState:
        send_state(provisioning_ ? State::Provisioning : url.empty() ? State::Ready : State::Provisioned);
        if (!provisioning_ && !url.empty()) {
            hooks_.send(rpc_result(kCurrentState, {url}));
        }
        return;
    case kDeviceInfo:
        hooks_.send(rpc_result(kDeviceInfo, {info_.firmware, info_.version, info_.chip, info_.name}));
        return;
    case kScan: {
        const auto networks = hooks_.scan ? hooks_.scan() : std::vector<Network>{};
        for (const auto &n : networks) {
            hooks_.send(rpc_result(kScan, {n.ssid, std::to_string(n.rssi), n.secured ? "YES" : "NO"}));
        }
        hooks_.send(rpc_result(kScan, {})); // end of the list
        return;
    }
    default:
        send_error(Error::UnknownRpc);
        return;
    }
}

void Improv::connected(bool ok)
{
    std::lock_guard lock(mu_);
    if (!provisioning_) {
        return;
    }
    provisioning_ = false;
    if (!ok) {
        send_error(Error::UnableToConnect);
        send_state(State::Ready);
        return;
    }
    send_state(State::Provisioned);
    hooks_.send(rpc_result(kWifiSettings, {hooks_.url ? hooks_.url() : std::string()}));
}

void Improv::send_state(State s)
{
    hooks_.send(packet(kTypeState, std::string(1, static_cast<char>(s))));
}

void Improv::send_error(Error e)
{
    hooks_.send(packet(kTypeError, std::string(1, static_cast<char>(e))));
}

} // namespace core
