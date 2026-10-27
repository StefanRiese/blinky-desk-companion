// SourceRouter: decides for every configured source who fetches it, as in the emulator
// (host/internal/sim/device.go, decideLocked):
// - via auto (default): the device itself when its WiFi has internet and the source is
//   device-native; otherwise the first host link (USB before TCP) that offers the source
//   (and has internet if the source needs it)
// - via device / via host force one side
// Each source goes to one place only; route() moves it when a link or the internet comes or
// goes. Call everything from the task that owns the HostLinks (the LVGL task).
#pragma once

#include <string>
#include <vector>

#include "app_core/device_fetcher.h"
#include "app_core/host_link.h"
#include "app_core/settings.h"
#include "app_core/sources.h"

namespace core {

class SourceRouter {
public:
    struct Status {
        std::string sid, src;
        std::string via;   // device | host | none
        std::string link;  // the HostLink's name when via is host
        std::string state; // pending | ok | error | backoff | unavailable
        std::string msg;
        int every = 0;
    };

    // links in order of preference; device may be null (no WiFi in the simulator and tests).
    SourceRouter(const Settings &settings, std::vector<HostLink *> links, DeviceFetcher *device);

    // set_device_net tells whether the device's WiFi has internet (routes again if it changed).
    void set_device_net(bool internet);
    bool device_net() const { return device_net_; }

    // route assigns all sources again (after settings, link or internet changes).
    void route();

    std::vector<Status> status() const;

private:
    struct Decision {
        std::string via, why;
        HostLink *link = nullptr;
    };
    Decision decide(const SourceSpec &s) const;

    const Settings &settings_;
    std::vector<HostLink *> links_;
    DeviceFetcher *device_;
    bool device_net_ = false;
    std::vector<SourceSpec> specs_;
    std::vector<Decision> decisions_;
};

} // namespace core
