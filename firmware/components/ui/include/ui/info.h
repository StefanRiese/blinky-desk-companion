// Info screen: firmware and board, the WiFi and host links (USB, TCP), every configured
// source with who fetches it and how that goes, and a touch tester (a dot follows the
// finger, taps are counted). Refreshes once a second.
#pragma once

#include "lvgl.h"
#include "ui/status_bar.h"

namespace ui {

struct InfoContext {
    const char *version; // firmware version
    const char *board;   // board name, e.g. "ws146"
    StatusFn status;     // may be empty (nothing connected)
};

class InfoPage {
public:
    static constexpr int kMaxSources = 5; // rows; more are summed up in the last one

    static InfoPage *create(lv_obj_t *parent, const InfoContext &ctx);
    void refresh();
    // touches returns how many taps the touch tester has counted.
    int touches() const { return taps_; }

private:
    explicit InfoPage(const InfoContext &ctx) : ctx_(ctx) {}
    static void on_touch(lv_event_t *e);
    static void on_timer(lv_timer_t *t);
    static void on_delete(lv_event_t *e);

    struct Row {
        lv_obj_t *box = nullptr, *icon = nullptr, *text = nullptr;
    };

    InfoContext ctx_;
    lv_timer_t *timer_ = nullptr;
    Row wifi_, usb_, tcp_;
    Row sources_[kMaxSources];
    lv_obj_t *dot_ = nullptr, *touch_label_ = nullptr;
    int taps_ = 0;
};

} // namespace ui
