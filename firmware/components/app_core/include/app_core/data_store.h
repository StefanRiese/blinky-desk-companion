// DataStore: every value the buddy shows (indoor.temp, weather.now.temp, user.coffee, …)
// with where it came from, when, and how long it stays valid. Same rules as the browser
// emulator (host/internal/sim/store.go): a value from a higher-priority source is kept
// until it goes stale. Thread-safe; the UI notices changes through version().
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace core {

using Value = std::variant<std::monostate, bool, double, std::string>;

// Origin of a value, lowest priority first. Device and host fetches rank the same.
enum class Origin : uint8_t { Push, Host, Device, Local };
std::string_view origin_name(Origin o);

struct Item {
    Value value;
    Origin origin;
    uint32_t age_ms;
    uint32_t ttl_ms; // 0 = never stale
    bool stale;
};

class DataStore {
public:
    static constexpr size_t kMaxEntries = 128;
    using NowMs = std::function<uint32_t()>;

    explicit DataStore(NowMs now);

    // put stores a value unless a fresh value from a higher-priority origin exists, or the
    // store is full. Returns whether the value was taken.
    bool put(std::string_view key, Value value, Origin origin, uint32_t ttl_ms);
    void remove(std::string_view key);

    std::optional<Item> get(std::string_view key) const;
    // number and text return the value only if it is fresh and of that type.
    std::optional<double> number(std::string_view key) const;
    std::optional<std::string> text(std::string_view key) const;

    // items lists the entries whose key starts with prefix, sorted by key.
    std::vector<std::pair<std::string, Item>> items(std::string_view prefix = {}) const;

    // version changes with every accepted put or remove.
    uint32_t version() const;

private:
    struct Entry {
        Value value;
        Origin origin;
        uint32_t ts;
        uint32_t ttl;
    };
    Item item(const Entry &e, uint32_t now) const;

    NowMs now_;
    mutable std::mutex mu_;
    std::map<std::string, Entry, std::less<>> m_;
    uint32_t version_ = 0;
};

} // namespace core
