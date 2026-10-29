// Custom pages: a host (or a script through buddy-link) sends a "page" message with a title
// and widgets, and the device shows it after its built-in pages, until it is removed or its
// ttl runs out. Widgets as in protocol/PROTOCOL.md: value, bar, icon+text, list, sparkline.
// Mirrors the emulator (host/internal/sim/device.go, customPage).
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ArduinoJson.h>

#include "app_core/data_store.h"

namespace core {

struct Widget {
    std::string kind; // value | bar | icon+text | list | sparkline
    std::string label, text, icon, unit;
    Value value;             // value, bar
    double max = 100;        // bar
    std::optional<double> warn, crit;
    std::vector<std::string> lines; // list (max 6)
    std::vector<double> values;     // sparkline (max 48)
};

struct CustomPage {
    std::string name, title;
    std::vector<Widget> items; // max kMaxItems
    uint32_t expires_ms = 0;   // 0 = until removed
};

class CustomPages {
public:
    static constexpr size_t kMaxPages = 8, kMaxItems = 8;

    // handle takes a "page" message: stores, replaces or (del) removes a page. Returns
    // whether something changed.
    bool handle(JsonVariantConst m, uint32_t now_ms);
    // expire removes pages whose ttl ran out; returns whether one went.
    bool expire(uint32_t now_ms);

    // names lists the pages, sorted (the swipe order).
    std::vector<std::string> names() const;
    const CustomPage *get(std::string_view name) const;
    // version changes with every change, so the UI knows when to redraw.
    uint32_t version() const { return version_; }

private:
    std::map<std::string, CustomPage, std::less<>> pages_;
    uint32_t version_ = 0;
};

} // namespace core
