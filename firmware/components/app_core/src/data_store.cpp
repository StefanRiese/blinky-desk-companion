#include "app_core/data_store.h"

namespace core {

std::string_view origin_name(Origin o)
{
    switch (o) {
    case Origin::Local:
        return "local";
    case Origin::Device:
        return "device";
    case Origin::Host:
        return "host";
    case Origin::Push:
        break;
    }
    return "push";
}

namespace {

// Device and host fetches rank the same (the SourceRouter picks one of them per source).
int rank(Origin o)
{
    switch (o) {
    case Origin::Local:
        return 3;
    case Origin::Device:
    case Origin::Host:
        return 2;
    case Origin::Push:
        break;
    }
    return 1;
}

bool is_stale(uint32_t ts, uint32_t ttl, uint32_t now)
{
    return ttl > 0 && now - ts > ttl;
}

} // namespace

DataStore::DataStore(NowMs now) : now_(std::move(now)) {}

bool DataStore::put(std::string_view key, Value value, Origin origin, uint32_t ttl_ms)
{
    const uint32_t now = now_();
    std::lock_guard lock(mu_);
    if (!put_locked(key, std::move(value), origin, ttl_ms, now)) {
        return false;
    }
    version_++;
    return true;
}

size_t DataStore::put_many(std::vector<Write> writes)
{
    const uint32_t now = now_();
    std::lock_guard lock(mu_);
    size_t n = 0;
    for (Write &w : writes) {
        n += put_locked(w.key, std::move(w.value), w.origin, w.ttl_ms, now) ? 1 : 0;
    }
    if (n > 0) {
        version_++;
    }
    return n;
}

bool DataStore::put_locked(std::string_view key, Value &&value, Origin origin, uint32_t ttl_ms, uint32_t now)
{
    auto it = m_.find(key);
    if (it != m_.end()) {
        const Entry &old = it->second;
        if (!is_stale(old.ts, old.ttl, now) && rank(old.origin) > rank(origin)) {
            return false;
        }
        it->second = Entry{std::move(value), origin, now, ttl_ms};
    } else {
        if (m_.size() >= kMaxEntries) {
            // Make room: keys of renamed or removed sources go stale and would fill the
            // store for good. Drop the one that went stale first.
            auto oldest = m_.end();
            uint32_t most = 0;
            for (auto e = m_.begin(); e != m_.end(); ++e) {
                const Entry &x = e->second;
                if (is_stale(x.ts, x.ttl, now) && (oldest == m_.end() || now - x.ts - x.ttl > most)) {
                    oldest = e;
                    most = now - x.ts - x.ttl;
                }
            }
            if (oldest == m_.end()) {
                return false;
            }
            m_.erase(oldest);
        }
        m_.emplace(std::string(key), Entry{std::move(value), origin, now, ttl_ms});
    }
    return true;
}

void DataStore::remove(std::string_view key)
{
    std::lock_guard lock(mu_);
    auto it = m_.find(key);
    if (it != m_.end()) {
        m_.erase(it);
        version_++;
    }
}

Item DataStore::item(const Entry &e, uint32_t now) const
{
    return Item{e.value, e.origin, now - e.ts, e.ttl, is_stale(e.ts, e.ttl, now)};
}

std::optional<Item> DataStore::get(std::string_view key) const
{
    const uint32_t now = now_();
    std::lock_guard lock(mu_);
    auto it = m_.find(key);
    if (it == m_.end()) {
        return std::nullopt;
    }
    return item(it->second, now);
}

std::optional<double> DataStore::number(std::string_view key) const
{
    const auto i = get(key);
    if (!i || i->stale) {
        return std::nullopt;
    }
    if (const auto *d = std::get_if<double>(&i->value)) {
        return *d;
    }
    return std::nullopt;
}

std::optional<std::string> DataStore::text(std::string_view key) const
{
    const auto i = get(key);
    if (!i || i->stale) {
        return std::nullopt;
    }
    if (const auto *s = std::get_if<std::string>(&i->value)) {
        return *s;
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, Item>> DataStore::items(std::string_view prefix) const
{
    const uint32_t now = now_();
    std::lock_guard lock(mu_);
    std::vector<std::pair<std::string, Item>> out;
    for (auto it = m_.lower_bound(prefix); it != m_.end() && it->first.starts_with(prefix); ++it) {
        out.emplace_back(it->first, item(it->second, now));
    }
    return out;
}

uint32_t DataStore::version() const
{
    std::lock_guard lock(mu_);
    return version_;
}

} // namespace core
