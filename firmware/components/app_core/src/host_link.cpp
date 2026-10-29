#include "app_core/host_link.h"

#include <algorithm>

#include "app_core/settings_json.h"

namespace core {
namespace {

std::string text(JsonVariantConst v)
{
    return v.is<const char *>() ? std::string(v.as<const char *>()) : std::string();
}

std::string serialize(const JsonDocument &doc)
{
    std::string out;
    serializeJson(doc, out);
    return out;
}

} // namespace

HostLink::HostLink(std::string name, Settings &settings, DataStore &store, proto::DeviceInfo info, Hooks hooks)
    : name_(std::move(name)), settings_(settings), store_(store), info_(std::move(info)), hooks_(std::move(hooks))
{
}

void HostLink::up(uint32_t now)
{
    now_ = now;
    peer_ = Peer::Host;
    last_recv_ = now;
    send_hello(now);
}

void HostLink::down()
{
    const bool was_serving = serving();
    state_ = State::Off;
    host_.clear();
    srcs_.clear();
    drop_subs();
    if (was_serving) {
        routing();
    }
}

void HostLink::relink(uint32_t now)
{
    if (state_ != State::Off && peer_ == Peer::Host) {
        lost(now);
    }
}

void HostLink::send(const std::string &line, uint32_t now)
{
    if (hooks_.send) {
        hooks_.send(line);
    }
    last_send_ = now;
}

void HostLink::send_hello(uint32_t now)
{
    // The setup page is not a host: it gets no link token.
    const std::string token = peer_ == Peer::Host ? settings_.get("link.token") : std::string();
    state_ = State::Handshake;
    hello_at_ = now;
    send(proto::hello(settings_.get("dev"), info_, token), now);
}

void HostLink::lost(uint32_t now)
{
    const bool was_serving = serving();
    host_.clear();
    srcs_.clear();
    drop_subs();
    peer_ = Peer::Host;
    state_ = State::Handshake;
    if (was_serving) {
        routing();
    }
    send_hello(now);
}

void HostLink::drop_subs()
{
    subs_.clear();
    keys_.clear();
    pending_.clear();
}

void HostLink::routing()
{
    if (hooks_.routing) {
        hooks_.routing();
    }
}

HostLink::Sub *HostLink::find(std::string_view sid)
{
    const auto it = std::find_if(subs_.begin(), subs_.end(), [&](const Sub &s) { return s.sid == sid; });
    return it == subs_.end() ? nullptr : &*it;
}

const HostLink::Sub *HostLink::sub(std::string_view sid) const
{
    const auto it = std::find_if(subs_.begin(), subs_.end(), [&](const Sub &s) { return s.sid == sid; });
    return it == subs_.end() ? nullptr : &*it;
}

bool HostLink::offers(std::string_view src) const
{
    return std::find(srcs_.begin(), srcs_.end(), src) != srcs_.end();
}

void HostLink::tick(uint32_t now)
{
    now_ = now;
    if (state_ == State::Handshake && now - hello_at_ >= kHelloRetryMs) {
        send_hello(now);
    } else if (state_ == State::Ready) {
        if (now - last_recv_ >= kSilenceMs) {
            lost(now);
            if (hooks_.silent) {
                hooks_.silent();
            }
        } else if (now - last_send_ >= kPingAfterMs) {
            send(proto::ping(), now);
        }
    }
}

void HostLink::on_line(std::string_view line, uint32_t now)
{
    now_ = now;
    JsonDocument doc;
    if (state_ == State::Off || deserializeJson(doc, line.data(), line.size()) != DeserializationError::Ok ||
        !doc.is<JsonObject>()) {
        return;
    }
    last_recv_ = now;
    const JsonVariantConst m = doc.as<JsonVariantConst>();
    const std::string t = text(m["t"]);
    if (t == "hello") {
        handle_hello(m, now);
        return;
    }
    if (state_ != State::Ready) {
        return;
    }
    if (t == "config") {
        handle_config(m, now);
        return;
    }
    if (peer_ == Peer::Setup) {
        return; // the setup page only reads and writes settings
    }
    if (t == "ota") {
        if (hooks_.ota) {
            if (const std::string reply = hooks_.ota(m); !reply.empty()) {
                send(reply, now);
            }
        } else if (m["id"].is<int>()) {
            send(proto::err(m["id"].as<int>(), "ota", "this firmware takes no updates over the link"), now);
        }
        return;
    }
    if (t == "host_state") {
        const bool net = m["net"] | true;
        if (net != host_net_) {
            host_net_ = net;
            routing();
        }
    } else if (t == "ack") {
        const auto it = pending_.find(m["id"] | -1);
        if (it != pending_.end()) {
            if (Sub *s = find(it->second); s != nullptr && (m["every"] | 0) > 0) {
                s->every = m["every"];
            }
            pending_.erase(it);
        }
    } else if (t == "err") {
        const std::string code = text(m["code"]), msg = text(m["msg"]);
        const auto it = pending_.find(m["id"] | -1);
        if (it != pending_.end()) {
            if (Sub *s = find(it->second)) {
                s->state = "error";
                s->msg = code + ": " + msg;
            }
            pending_.erase(it);
        }
        if (code == "auth" || code == "version") {
            error_ = code + ": " + msg;
        }
    } else if (t == "sub_state") {
        if (Sub *s = find(text(m["sid"]))) {
            s->state = text(m["state"]);
            s->msg = text(m["msg"]);
        }
    } else if (t == "time" || t == "weather" || t == "data") {
        handle_result(m, t);
    } else if (t == "page") {
        if (hooks_.page) {
            hooks_.page(m);
        }
    } else if (t == "notify") {
        if (hooks_.notify && !text(m["text"]).empty()) {
            const int dur = m["dur"] | 0;
            hooks_.notify({.text = text(m["text"]),
                           .icon = text(m["icon"]),
                           .mood = text(m["mood"]),
                           .sound = text(m["sound"]),
                           .dur = dur > 0 ? std::min(dur, 600) : 8});
        }
    } else if (t == "mood") {
        if (hooks_.mood && m["mood"].is<const char *>()) {
            hooks_.mood(text(m["mood"]), m["ttl"] | 30);
        }
    }
    // notify and page follow with the notification overlay and the PageManager (M7).
}

void HostLink::handle_hello(JsonVariantConst m, uint32_t now)
{
    if ((m["v"] | proto::kVersion) != proto::kVersion) {
        send(proto::err(std::nullopt, "version", "protocol version 1 expected"), now);
        return;
    }
    const Peer peer = text(m["role"]) == "setup" ? Peer::Setup : Peer::Host;
    const std::string dev = text(m["dev"]);
    if (dev.empty()) {
        // The other side opened the port and asks us to introduce ourselves. If our own
        // hello to the same kind of peer is on its way, its answer will come.
        const bool waiting = state_ == State::Handshake && peer == peer_ && now - hello_at_ < kHelloRetryMs;
        if (!waiting) {
            const bool was_serving = serving();
            if (state_ == State::Ready) {
                host_.clear(); // a new peer took the port over
                srcs_.clear();
                drop_subs();
            }
            peer_ = peer;
            send_hello(now);
            if (was_serving) {
                routing();
            }
        }
        return;
    }
    if (dev != settings_.get("dev")) {
        return;
    }
    peer_ = peer;
    host_ = text(m["host"]);
    srcs_.clear();
    for (JsonVariantConst s : m["srcs"].as<JsonArrayConst>()) {
        srcs_.push_back(text(s));
    }
    host_net_ = m["net"] | true;
    error_.clear();
    state_ = State::Ready;
    drop_subs(); // a new hello means the host has no subscriptions
    routing();
    if (serving() && !web_.empty()) {
        send(proto::web(web_), now);
    }
}

void HostLink::assign(const std::vector<SourceSpec> &specs)
{
    if (!serving()) {
        return;
    }
    auto wanted = [&](const Sub &s) {
        return std::any_of(specs.begin(), specs.end(),
                           [&](const SourceSpec &spec) { return spec.sid == s.sid && spec.key() == keys_[s.sid]; });
    };
    for (auto it = subs_.begin(); it != subs_.end();) {
        if (wanted(*it)) {
            ++it;
            continue;
        }
        send(proto::unsub(it->sid), now_);
        keys_.erase(it->sid);
        std::erase_if(pending_, [&](const auto &p) { return p.second == it->sid; });
        it = subs_.erase(it);
    }
    for (const SourceSpec &spec : specs) {
        if (find(spec.sid) != nullptr) {
            continue;
        }
        const int id = next_id_++;
        pending_[id] = spec.sid;
        keys_[spec.sid] = spec.key();
        subs_.push_back({.sid = spec.sid, .src = spec.src, .state = "pending", .msg = "", .every = spec.every});
        send(proto::sub(id, spec.sid, spec.src, spec.every, spec.params), now_);
    }
}

void HostLink::handle_result(JsonVariantConst m, std::string_view type)
{
    if (type == "time") {
        if (m["epoch"].is<int64_t>() && hooks_.set_time) {
            hooks_.set_time(m["epoch"].as<int64_t>());
        }
        mark_ok("time", true);
        return;
    }
    if (type == "weather") {
        store_weather(store_, m, Origin::Host, (m["ttl"] | 3600u) * 1000u);
        mark_ok("weather", true);
        return;
    }
    for (const auto &sid : store_data(store_, m["kv"].as<JsonObjectConst>(), Origin::Host, (m["ttl"] | 0u) * 1000u)) {
        mark_ok(sid, false);
    }
    for (JsonVariantConst k : m["del"].as<JsonArrayConst>()) {
        store_.remove(text(k));
    }
}

void HostLink::mark_ok(std::string_view name, bool by_src)
{
    for (Sub &s : subs_) {
        if (by_src ? s.src == name : s.sid == name) {
            s.state = "ok";
            s.msg.clear();
        }
    }
}

void HostLink::handle_config(JsonVariantConst m, uint32_t now)
{
    const std::optional<int> id = m["id"].is<int>() ? std::optional<int>(m["id"].as<int>()) : std::nullopt;

    if (m["schema_from"].is<int>()) {
        JsonDocument reply;
        reply["t"] = "config";
        if (id) {
            reply["id"] = *id;
        }
        const int from = std::max(0, m["schema_from"].as<int>());
        // Room for {"t":"config","id":…,"schema":…,"next":…}.
        const size_t next =
            settings_schema_page(reply["schema"].to<JsonArray>(), static_cast<size_t>(from), proto::kMaxLine - 64);
        if (next > 0) {
            reply["next"] = next;
        }
        send(serialize(reply), now);
        return;
    }
    if (m["get"].is<JsonArrayConst>()) {
        JsonDocument reply;
        reply["t"] = "config";
        if (id) {
            reply["id"] = *id;
        }
        JsonObject values = reply["values"].to<JsonObject>();
        for (JsonVariantConst k : m["get"].as<JsonArrayConst>()) {
            settings_values(values, settings_, text(k));
        }
        send(serialize(reply), now);
        return;
    }
    if (!m["set"].is<JsonObjectConst>()) {
        return;
    }
    std::vector<std::string> keys;
    if (auto e = apply_settings(settings_, m["set"].as<JsonObjectConst>(), keys)) {
        send(proto::err(id, "bad_value", *e), now);
        return;
    }
    if (id) {
        send(proto::ack(*id), now);
    }
    const bool relink_needed =
        std::any_of(keys.begin(), keys.end(), [](const std::string &k) { return k == "dev" || k == "link.token"; });
    if (relink_needed && peer_ == Peer::Host) {
        lost(now); // the host knows us by dev (and token): introduce ourselves again
    }
    if (hooks_.changed) {
        hooks_.changed(keys); // sources and location: the router assigns the sources again
    }
}

void HostLink::set_web(std::string_view url, uint32_t now)
{
    if (url == web_) {
        return;
    }
    web_ = std::string(url);
    if (serving()) {
        send(proto::web(web_), now);
    }
}

void HostLink::send_event(std::string_view kind, std::string_view name, std::string_view state)
{
    if (serving()) {
        send(proto::event(kind, name, state), now_);
    }
}

} // namespace core
