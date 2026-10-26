#include "app_core/host_link.h"

#include <algorithm>
#include <cmath>

namespace core {
namespace {

// Sources that need internet on the fetching side (proxmox/ha are LAN services).
bool needs_internet(std::string_view src)
{
    return src == "weather" || src == "aqi" || src == "http" || src == "gitlab" || src == "calendar";
}

bool starts_with(std::string_view s, std::string_view prefix)
{
    return s.substr(0, prefix.size()) == prefix;
}

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

// add_setting puts one setting into a config reply, typed as in the registry.
void add_setting(JsonObject values, const Setting &s, const Settings &settings)
{
    const std::string key(s.key);
    switch (s.type) {
    case Type::Bool:
        values[key] = settings.flag(s.key);
        break;
    case Type::Int:
        values[key] = static_cast<long long>(settings.number(s.key));
        break;
    case Type::Float:
        values[key] = settings.number(s.key);
        break;
    case Type::Json: {
        JsonDocument tmp;
        const std::string v = settings.get(s.key);
        if (deserializeJson(tmp, v) == DeserializationError::Ok) {
            values[key] = tmp.as<JsonVariantConst>();
        } else {
            values[key] = v;
        }
        break;
    }
    case Type::String:
    case Type::Enum:
        values[key] = settings.shown(s.key);
        break;
    }
}

} // namespace

HostLink::HostLink(Settings &settings, DataStore &store, proto::DeviceInfo info, Hooks hooks)
    : settings_(settings), store_(store), info_(std::move(info)), hooks_(std::move(hooks))
{
    reroute();
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
    state_ = State::Off;
    host_.clear();
    srcs_.clear();
    drop_host_subs();
    reroute();
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
    host_.clear();
    srcs_.clear();
    drop_host_subs();
    peer_ = Peer::Host;
    state_ = State::Handshake;
    reroute();
    send_hello(now);
}

void HostLink::drop_host_subs()
{
    for (auto it = assign_.begin(); it != assign_.end();) {
        it = it->second.sub.via == "host" ? assign_.erase(it) : std::next(it);
    }
    pending_.clear();
}

void HostLink::tick(uint32_t now)
{
    now_ = now;
    if (state_ == State::Handshake && now - hello_at_ >= kHelloRetryMs) {
        send_hello(now);
    } else if (state_ == State::Ready) {
        if (now - last_recv_ >= kSilenceMs) {
            lost(now);
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
    if (t == "host_state") {
        host_net_ = m["net"] | true;
        reroute();
    } else if (t == "ack") {
        const auto it = pending_.find(m["id"] | -1);
        if (it != pending_.end()) {
            if (auto a = assign_.find(it->second); a != assign_.end() && (m["every"] | 0) > 0) {
                a->second.sub.every = m["every"];
            }
            pending_.erase(it);
        }
    } else if (t == "err") {
        const std::string code = text(m["code"]), msg = text(m["msg"]);
        const auto it = pending_.find(m["id"] | -1);
        if (it != pending_.end()) {
            if (auto a = assign_.find(it->second); a != assign_.end()) {
                a->second.sub.state = "error";
                a->second.sub.msg = code + ": " + msg;
            }
            pending_.erase(it);
        }
        if (code == "auth" || code == "version") {
            error_ = code + ": " + msg;
        }
    } else if (t == "sub_state") {
        if (auto a = assign_.find(text(m["sid"])); a != assign_.end() && a->second.sub.via == "host") {
            a->second.sub.state = text(m["state"]);
            a->second.sub.msg = text(m["msg"]);
        }
    } else if (t == "time" || t == "weather" || t == "data") {
        handle_result(m, t);
    }
    // mood, notify and page follow with the MoodEngine and the PageManager (M7).
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
            if (state_ == State::Ready) {
                host_.clear(); // a new peer took the port over
                srcs_.clear();
                drop_host_subs();
            }
            peer_ = peer;
            send_hello(now);
            reroute();
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
    drop_host_subs(); // a new hello means the host has no subscriptions
    reroute();
}

void HostLink::reroute()
{
    JsonDocument cfg;
    deserializeJson(cfg, settings_.get("sources"));
    const bool host_ready = state_ == State::Ready && peer_ == Peer::Host;

    std::vector<std::string> seen;
    for (JsonVariantConst c : cfg.as<JsonArrayConst>()) {
        const std::string sid = text(c["sid"]), src = text(c["src"]), via_cfg = text(c["via"]);
        const int every = c["every"] | 0;
        if (!proto::valid_sid(sid) || src.empty() || std::find(seen.begin(), seen.end(), sid) != seen.end()) {
            continue;
        }
        seen.push_back(sid);

        // Who fetches it: the host if it offers the source (and has internet if needed).
        // Fetching on the device itself follows with WiFi (M4).
        const bool offers = std::find(srcs_.begin(), srcs_.end(), src) != srcs_.end();
        const bool host_ok = host_ready && offers && (!needs_internet(src) || host_net_);
        std::string via = "none", why;
        if (via_cfg == "device") {
            why = "device fetching needs WiFi (not yet)";
        } else if (host_ok) {
            via = "host";
        } else if (!host_ready) {
            why = "no host connected";
        } else if (!offers) {
            why = "host doesn't offer " + src;
        } else {
            why = "host has no internet";
        }

        // Parameters with the device's location for weather and air quality.
        JsonDocument p;
        p.set(c["p"]);
        if ((src == "weather" || src == "aqi") && !p["lat"].is<double>()) {
            p["lat"] = settings_.number("location.lat");
            p["lon"] = settings_.number("location.lon");
        }
        const std::string params = p.isNull() ? std::string() : serialize(p);
        const std::string key = via + "|" + std::to_string(every) + "|" + params;

        auto it = assign_.find(sid);
        if (it != assign_.end() && it->second.key == key) {
            if (via == "none") {
                it->second.sub.msg = why;
            }
            continue;
        }
        if (it != assign_.end() && it->second.sub.via == "host" && host_ready) {
            send(proto::unsub(sid), now_);
        }
        Assignment a{.sub = {.sid = sid, .src = src, .via = via, .state = "pending", .msg = "", .every = every},
                     .key = key};
        if (via == "host") {
            const int id = next_id_++;
            pending_[id] = sid;
            send(proto::sub(id, sid, src, every, params), now_);
        } else {
            a.sub.state = "unavailable";
            a.sub.msg = why;
        }
        assign_[sid] = std::move(a);
    }
    for (auto it = assign_.begin(); it != assign_.end();) {
        if (std::find(seen.begin(), seen.end(), it->first) != seen.end()) {
            ++it;
            continue;
        }
        if (it->second.sub.via == "host" && host_ready) {
            send(proto::unsub(it->first), now_);
        }
        it = assign_.erase(it);
    }
    order_ = seen;
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
        const uint32_t ttl = (m["ttl"] | 3600u) * 1000u;
        const JsonVariantConst now = m["now"];
        for (const char *k : {"temp", "feels", "hum", "code", "wind", "is_day"}) {
            if (now[k].is<double>()) {
                store_.put(std::string("weather.now.") + k, now[k].as<double>(), Origin::Host, ttl);
            }
        }
        if (m["rain_next_2h"].is<JsonArrayConst>()) {
            double rain = 0;
            for (JsonVariantConst r : m["rain_next_2h"].as<JsonArrayConst>()) {
                rain += r | 0.0;
            }
            store_.put("weather.rain_next_2h", std::round(rain * 10) / 10, Origin::Host, ttl);
        }
        mark_ok("weather", true);
        return;
    }
    // data: values by key; "user.*" was pushed by a script (lowest priority).
    const uint32_t ttl = (m["ttl"] | 0u) * 1000u;
    std::vector<std::string> sids;
    for (JsonPairConst kv : m["kv"].as<JsonObjectConst>()) {
        const std::string_view key(kv.key().c_str(), kv.key().size());
        const Origin origin = starts_with(key, "user.") ? Origin::Push : Origin::Host;
        const JsonVariantConst v = kv.value();
        if (v.is<bool>()) {
            store_.put(key, v.as<bool>(), origin, ttl);
        } else if (v.is<double>()) {
            store_.put(key, v.as<double>(), origin, ttl);
        } else if (v.is<const char *>()) {
            store_.put(key, std::string(v.as<const char *>()), origin, ttl);
        } else {
            continue;
        }
        if (const size_t dot = key.find('.'); dot != std::string_view::npos) {
            sids.emplace_back(key.substr(0, dot));
        }
    }
    for (JsonVariantConst k : m["del"].as<JsonArrayConst>()) {
        store_.remove(text(k));
    }
    for (const auto &sid : sids) {
        mark_ok(sid, false);
    }
}

void HostLink::mark_ok(std::string_view name, bool by_src)
{
    for (auto &[sid, a] : assign_) {
        if (a.sub.via == "host" && (by_src ? a.sub.src == name : sid == name)) {
            a.sub.state = "ok";
            a.sub.msg.clear();
        }
    }
}

void HostLink::handle_config(JsonVariantConst m, uint32_t now)
{
    const std::optional<int> id = m["id"].is<int>() ? std::optional<int>(m["id"].as<int>()) : std::nullopt;

    if (m["get"].is<JsonArrayConst>()) {
        JsonDocument reply;
        reply["t"] = "config";
        if (id) {
            reply["id"] = *id;
        }
        JsonObject values = reply["values"].to<JsonObject>();
        for (JsonVariantConst k : m["get"].as<JsonArrayConst>()) {
            const std::string want = text(k);
            for (const Setting &s : registry()) {
                if (want == "*" || s.key == want || starts_with(s.key, want + ".")) {
                    add_setting(values, s, settings_);
                }
            }
        }
        send(serialize(reply), now);
        return;
    }
    if (!m["set"].is<JsonObjectConst>()) {
        return;
    }
    std::vector<std::pair<std::string, std::string>> values;
    for (JsonPairConst kv : m["set"].as<JsonObjectConst>()) {
        std::string v;
        if (kv.value().is<const char *>()) {
            v = kv.value().as<const char *>();
        } else {
            serializeJson(kv.value(), v); // numbers, true/false, lists and objects as JSON text
        }
        values.emplace_back(std::string(kv.key().c_str(), kv.key().size()), std::move(v));
    }
    if (auto e = settings_.set_many(values)) {
        send(proto::err(id, "bad_value", *e), now);
        return;
    }
    if (id) {
        send(proto::ack(*id), now);
    }

    std::vector<std::string> keys;
    bool sources = false, relink = false;
    for (const auto &kv : values) {
        keys.push_back(kv.first);
        sources = sources || kv.first == "sources" || starts_with(kv.first, "location.");
        relink = relink || kv.first == "dev" || kv.first == "link.token";
    }
    if (hooks_.changed) {
        hooks_.changed(keys);
    }
    if (relink && peer_ == Peer::Host) {
        lost(now); // the host knows us by dev (and token): introduce ourselves again
    } else if (sources) {
        reroute();
    }
}

void HostLink::send_event(std::string_view kind, std::string_view name, std::string_view state)
{
    if (state_ == State::Ready && peer_ == Peer::Host) {
        send(proto::event(kind, name, state), now_);
    }
}

std::vector<HostLink::Sub> HostLink::subs() const
{
    std::vector<Sub> out;
    for (const auto &sid : order_) {
        if (auto it = assign_.find(sid); it != assign_.end()) {
            out.push_back(it->second.sub);
        }
    }
    return out;
}

} // namespace core
