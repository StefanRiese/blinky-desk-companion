// HostLink: the device side of one link to buddy-link (or the setup page), as in
// protocol/PROTOCOL.md and the emulator (host/internal/sim/device.go):
// - handshake: hello, answer the host's hello without dev, resend after 5 s; ping after
//   10 s without sending; the link is lost after 30 s of silence
// - subscriptions: the SourceRouter decides which sources this link carries (assign); they
//   are sent again after every new hello
// - results: time (set_time hook), weather and data into the DataStore; "mood" (mood hook)
// - config get/set from the host or the setup page ("role":"setup": only config, no subs)
// One HostLink per transport (USB, TCP). Not thread-safe: call everything from one task
// (the firmware uses the LVGL task).
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
#include "app_core/sources.h"

namespace core {

// Notification is a host's "notify" message (protocol/PROTOCOL.md).
struct Notification {
    std::string text, icon, mood, sound;
    int dur = 8; // seconds on screen
};

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
        // routing: the link started or stopped serving a host, or what the host can fetch
        // changed. The SourceRouter then assigns the sources again.
        std::function<void()> routing;
        // silent: 30 s without a word from the host. Serial says hello again by itself; TCP
        // closes the connection here and reconnects.
        std::function<void()> silent;
        // mood: the host's mood override, for ttl seconds.
        std::function<void(std::string_view mood, int ttl)> mood;
        // notify: a notification to show.
        std::function<void(const Notification &n)> notify;
        // ota: a firmware update message (OtaReceiver::handle); returns the reply line.
        std::function<std::string(JsonVariantConst m)> ota;
    };

    // One subscription on this link.
    struct Sub {
        std::string sid, src;
        std::string state; // pending | ok | error | backoff
        std::string msg;
        int every = 0;
    };

    // name says which transport this is ("usb", "tcp"); it shows on the info screen.
    HostLink(std::string name, Settings &settings, DataStore &store, proto::DeviceInfo info, Hooks hooks);

    // up is called when the transport is connected (serial: at start), down when it is lost.
    void up(uint32_t now);
    void down();
    void on_line(std::string_view line, uint32_t now);
    void tick(uint32_t now);
    // relink says hello again, e.g. because dev or the link token changed.
    void relink(uint32_t now);

    // assign sets the subscriptions this link carries: new ones are subscribed, changed ones
    // renewed, missing ones unsubscribed. Ignored unless the link serves a host.
    void assign(const std::vector<SourceSpec> &specs);
    // send_event reports a device event to the host (countdown, alarm, touch, …).
    void send_event(std::string_view kind, std::string_view name, std::string_view state = {});
    // set_web tells the host where the device's web page is ("" = none). The host gets it
    // after each hello and whenever it changes.
    void set_web(std::string_view url, uint32_t now);

    const std::string &name() const { return name_; }
    State state() const { return state_; }
    Peer peer() const { return peer_; }
    bool ready() const { return state_ == State::Ready; }
    // serving: a host (not the setup page) is connected and can take subscriptions.
    bool serving() const { return state_ == State::Ready && peer_ == Peer::Host; }
    bool offers(std::string_view src) const;
    const std::string &host_name() const { return host_; }
    bool host_net() const { return host_net_; }
    const std::string &error() const { return error_; } // auth/version errors from the host
    const std::vector<Sub> &subs() const { return subs_; }
    const Sub *sub(std::string_view sid) const;

private:
    void send(const std::string &line, uint32_t now);
    void send_hello(uint32_t now);
    void lost(uint32_t now);
    void drop_subs();
    void routing();
    Sub *find(std::string_view sid);
    void handle_hello(JsonVariantConst m, uint32_t now);
    void handle_config(JsonVariantConst m, uint32_t now);
    void handle_result(JsonVariantConst m, std::string_view type);
    void mark_ok(std::string_view src_or_sid, bool by_src);

    std::string name_;
    Settings &settings_;
    DataStore &store_;
    proto::DeviceInfo info_;
    Hooks hooks_;

    State state_ = State::Off;
    Peer peer_ = Peer::Host;
    std::string host_;
    std::vector<std::string> srcs_;
    bool host_net_ = true;
    std::string web_;
    std::string error_;
    uint32_t now_ = 0;
    uint32_t hello_at_ = 0, last_send_ = 0, last_recv_ = 0;
    int next_id_ = 1;
    std::map<int, std::string> pending_; // sub id → sid
    std::vector<Sub> subs_;              // in the order they were subscribed
    std::map<std::string, std::string, std::less<>> keys_; // sid → SourceSpec::key()
};

} // namespace core
