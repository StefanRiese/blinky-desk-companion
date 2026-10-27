// Status bar along the top of a page: time, outdoor temperature (greyed with a warning sign
// when it is stale) and icons for WiFi, the USB host and the TCP host. Each page has its own
// bar, so it slides along with the page. Items without data are left out.
#pragma once

#include <functional>

#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "app_core/status.h"
#include "lvgl.h"
#include "ui/dashboard.h"

namespace ui {

// StatusFn returns the current WiFi and link state.
using StatusFn = std::function<core::Status()>;

struct StatusContext {
    const core::DataStore *store;
    const core::Settings *settings;
    LocalTime local_time;
    StatusFn status;
};

class StatusBar {
public:
    // icons_only leaves out time and temperature (the dashboard shows them in large).
    static StatusBar *create(lv_obj_t *parent, const StatusContext &ctx, bool icons_only = false);
    void refresh();

private:
    StatusBar(const StatusContext &ctx, bool icons_only) : ctx_(ctx), icons_only_(icons_only) {}
    static void on_delete(lv_event_t *e);

    StatusContext ctx_;
    bool icons_only_;
    lv_obj_t *row_ = nullptr, *time_ = nullptr, *temp_ = nullptr, *stale_ = nullptr;
    lv_obj_t *wifi_ = nullptr, *usb_ = nullptr, *tcp_ = nullptr;
};

} // namespace ui
