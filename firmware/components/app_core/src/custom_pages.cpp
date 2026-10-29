#include "app_core/custom_pages.h"

namespace core {
namespace {

Value value_of(JsonVariantConst v)
{
    if (v.is<bool>()) {
        return v.as<bool>();
    }
    if (v.is<double>()) {
        return v.as<double>();
    }
    if (v.is<const char *>()) {
        return std::string(v.as<const char *>());
    }
    return {};
}

std::optional<double> number(JsonVariantConst v)
{
    return v.is<double>() ? std::optional<double>(v.as<double>()) : std::nullopt;
}

} // namespace

bool CustomPages::handle(JsonVariantConst m, uint32_t now_ms)
{
    const std::string name = m["name"] | "";
    if (name.empty()) {
        return false;
    }
    if (m["del"] | false) {
        if (pages_.erase(name) == 0) {
            return false;
        }
        version_++;
        return true;
    }
    if (!m["items"].is<JsonArrayConst>()) {
        return false;
    }
    if (!pages_.contains(name) && pages_.size() >= kMaxPages) {
        return false; // full: a page has to go first
    }
    CustomPage p;
    p.name = name;
    p.title = m["title"] | name;
    for (JsonVariantConst it : m["items"].as<JsonArrayConst>()) {
        if (p.items.size() >= kMaxItems) {
            break;
        }
        Widget w;
        w.kind = it["w"] | "";
        if (w.kind != "value" && w.kind != "bar" && w.kind != "icon+text" && w.kind != "list" && w.kind != "sparkline") {
            continue; // unknown widgets are skipped
        }
        w.label = it["label"] | "";
        w.text = it["text"] | "";
        w.icon = it["icon"] | "";
        w.unit = it["unit"] | "";
        w.value = value_of(it["value"]);
        w.max = it["max"] | 100.0;
        if (w.max <= 0) {
            w.max = 100;
        }
        w.warn = number(it["warn"]);
        w.crit = number(it["crit"]);
        for (JsonVariantConst l : it["lines"].as<JsonArrayConst>()) {
            if (w.lines.size() < 6) {
                w.lines.emplace_back(l | "");
            }
        }
        for (JsonVariantConst v : it["values"].as<JsonArrayConst>()) {
            if (w.values.size() < 48 && v.is<double>()) {
                w.values.push_back(v.as<double>());
            }
        }
        p.items.push_back(std::move(w));
    }
    const uint32_t ttl = m["ttl"] | 0u;
    p.expires_ms = ttl > 0 ? now_ms + ttl * 1000 + (now_ms + ttl * 1000 == 0) : 0;
    pages_[name] = std::move(p);
    version_++;
    return true;
}

bool CustomPages::expire(uint32_t now_ms)
{
    bool changed = false;
    for (auto it = pages_.begin(); it != pages_.end();) {
        if (it->second.expires_ms != 0 && static_cast<int32_t>(now_ms - it->second.expires_ms) >= 0) {
            it = pages_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) {
        version_++;
    }
    return changed;
}

std::vector<std::string> CustomPages::names() const
{
    std::vector<std::string> out;
    for (const auto &[name, p] : pages_) {
        out.push_back(name);
    }
    return out;
}

const CustomPage *CustomPages::get(std::string_view name) const
{
    const auto it = pages_.find(name);
    return it == pages_.end() ? nullptr : &it->second;
}

} // namespace core
