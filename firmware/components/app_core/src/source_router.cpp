#include "app_core/source_router.h"

namespace core {

SourceRouter::SourceRouter(const Settings &settings, std::vector<HostLink *> links, DeviceFetcher *device)
    : settings_(settings), links_(std::move(links)), device_(device)
{
}

void SourceRouter::set_device_net(bool internet)
{
    if (internet != device_net_) {
        device_net_ = internet;
        route();
    }
}

SourceRouter::Decision SourceRouter::decide(const SourceSpec &s) const
{
    const bool native = device_native(s.src);
    const bool device_ok = device_ != nullptr && native && device_net_;
    const std::string device_why = native ? "WiFi has no internet" : s.src + " is host-only";

    HostLink *host = nullptr;
    const HostLink *first_serving = nullptr;
    for (HostLink *l : links_) {
        if (!l->serving()) {
            continue;
        }
        if (first_serving == nullptr) {
            first_serving = l;
        }
        if (l->offers(s.src) && (!needs_internet(s.src) || l->host_net())) {
            host = l;
            break;
        }
    }
    std::string host_why = "no host connected";
    if (first_serving != nullptr) {
        host_why = first_serving->offers(s.src) ? "host has no internet" : "host doesn't offer " + s.src;
    }

    if (s.via == "device") {
        return device_ok ? Decision{"device", "", nullptr} : Decision{"none", device_why, nullptr};
    }
    if (s.via == "host") {
        return host ? Decision{"host", "", host} : Decision{"none", host_why, nullptr};
    }
    if (device_ok) {
        return {"device", "", nullptr};
    }
    if (host) {
        return {"host", "", host};
    }
    return {"none", device_why + ", " + host_why, nullptr};
}

void SourceRouter::route()
{
    specs_ = configured_sources(settings_);
    decisions_.clear();
    std::vector<std::vector<SourceSpec>> per_link(links_.size());
    std::vector<SourceSpec> on_device;
    for (const SourceSpec &s : specs_) {
        Decision d = decide(s);
        if (d.via == "device") {
            on_device.push_back(s);
        }
        for (size_t i = 0; i < links_.size(); i++) {
            if (links_[i] == d.link) {
                per_link[i].push_back(s);
            }
        }
        decisions_.push_back(std::move(d));
    }
    for (size_t i = 0; i < links_.size(); i++) {
        links_[i]->assign(per_link[i]);
    }
    if (device_ != nullptr) {
        device_->assign(on_device);
    }
}

std::vector<SourceRouter::Status> SourceRouter::status() const
{
    std::vector<Status> out;
    for (size_t i = 0; i < specs_.size(); i++) {
        const SourceSpec &s = specs_[i];
        const Decision &d = decisions_[i];
        Status st{.sid = s.sid, .src = s.src, .via = d.via, .link = "", .state = "unavailable", .msg = d.why,
                  .every = s.every};
        if (d.via == "device" && device_ != nullptr) {
            if (const auto f = device_->status(s.sid)) {
                st.state = f->state;
                st.msg = f->msg;
                st.every = f->every;
            }
        } else if (d.via == "host") {
            st.link = d.link->name();
            st.state = "pending";
            if (const HostLink::Sub *sub = d.link->sub(s.sid)) {
                st.state = sub->state;
                st.msg = sub->msg;
                st.every = sub->every;
            }
        }
        out.push_back(std::move(st));
    }
    return out;
}

} // namespace core
