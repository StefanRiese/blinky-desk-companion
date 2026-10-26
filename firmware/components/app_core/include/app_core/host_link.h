// HostLink: the device side of one link to buddyd (or the setup page), as in
// protocol/PROTOCOL.md and the emulator (host/internal/sim/device.go):
// - handshake: hello, answer the host's hello without dev, resend after 5 s; ping after
//   10 s without sending; the link is lost after 30 s of silence
// - subscriptions: one sub per configured source (setting "sources") the host offers;
//   moved away when the host loses internet, sent again after every new hello
// - results: time (set_time hook), weather and data into the DataStore
// - config get/set from the host or the setup page ("role":"setup": only config, no subs)
// Not thread-safe: call everything from one task (the firmware uses the LVGL task).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ArduinoJson.h>

#include "app_core/data_store.h"
#include "app_core/protocol.h"
#include "app_core/settings.h"

namespace core {

class HostLink {
public:
    static constexpr uint32_t kHelloRetryMs = 5000;
    static constexpr uint32_t kPingAfterMs = 10000;
    static constexpr uint32_t kSilenceMs = 30000;

    enum class State { Off, Handshake, Ready };
    enum class Peer { Host, Setup };

    struct Hooks {
        std::function<void(std::string_view line)> send;                   // one line, no '\n'
        std::function<void(int64_t epoch)> set_time;                       // from a "time" result
        std::function<void(const std::vector<std::string> &keys)> changed; // settings changed by config
    };

    // One configured source and what happened to it on this link.
    struct Sub {
        std::string sid, src;
        std::string via;   // "host" or "none"
        std::string state; // pending | ok | error | backoff | unavailable
        std::string msg;
        int every = 0;
    };

    HostLink(Settings &settings, DataStore &store, proto::DeviceInfo info, Hooks hooks);

    // up is called when the transport is connected (serial: at start), down when it is lost.
    void up(uint32_t now);
    void down();
    void on_line(std::string_view line, uint32_t now);
    void tick(uint32_t now);

    // reroute re-evaluates the sources (call after settings changed elsewhere).
    void reroute();
    // send_event reports a device event to the host (countdown, alarm, touch, …).
    void send_event(std::string_view kind, std::string_view name, std::string_view state = {});

    State state() const { return state_; }
    Peer peer() const { return peer_; }
    bool ready() const { return state_ == State::Ready; }
    const std::string &host_name() const { return host_; }
    bool host_net() const { return host_net_; }
    const std::string &error() const { return error_; } // auth/version errors from the host
    std::vector<Sub> subs() const;

private:
    struct Assignment {
        Sub sub;
        std::string key; // via|every|params: a change means unsub + sub
    };

    void send(const std::string &line, uint32_t now);
    void send_hello(uint32_t now);
    void lost(uint32_t now);
    void drop_host_subs();
    void handle_hello(JsonVariantConst m, uint32_t now);
    void handle_config(JsonVariantConst m, uint32_t now);
    void handle_result(JsonVariantConst m, std::string_view type);
    void mark_ok(std::string_view src_or_sid, bool by_src);

    Settings &settings_;
    DataStore &store_;
    proto::DeviceInfo info_;
    Hooks hooks_;

    State state_ = State::Off;
    Peer peer_ = Peer::Host;
    std::string host_;
    std::vector<std::string> srcs_;
    bool host_net_ = true;
    std::string error_;
    uint32_t now_ = 0;
    uint32_t hello_at_ = 0, last_send_ = 0, last_recv_ = 0;
    int next_id_ = 1;
    std::map<int, std::string> pending_; // sub id → sid
    std::map<std::string, Assignment> assign_;
    std::vector<std::string> order_; // sids in configuration order
};

} // namespace core
