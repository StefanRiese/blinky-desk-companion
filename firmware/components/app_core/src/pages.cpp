#include "app_core/pages.h"

#include <algorithm>

#include <ArduinoJson.h>

#include "app_core/settings.h"

namespace core {
namespace {

std::optional<std::vector<std::string>> parse(std::string_view json, std::string *error)
{
    auto fail = [&](std::string e) -> std::optional<std::vector<std::string>> {
        if (error != nullptr) {
            *error = std::move(e);
        }
        return std::nullopt;
    };
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok || !doc.is<JsonArrayConst>()) {
        return fail("ui.pages must be a list of page names");
    }
    std::vector<std::string> out;
    for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
        if (!v.is<const char *>()) {
            return fail("ui.pages must be a list of page names");
        }
        const std::string p = v.as<const char *>();
        if (std::find(kPages.begin(), kPages.end(), p) == kPages.end()) {
            std::string all;
            for (auto k : kPages) {
                all += (all.empty() ? "" : ", ") + std::string(k);
            }
            return fail("ui.pages: unknown page \"" + p + "\" (use " + all + ")");
        }
        if (std::find(out.begin(), out.end(), p) != out.end()) {
            return fail("ui.pages: \"" + p + "\" is listed twice");
        }
        out.push_back(p);
    }
    if (out.empty()) {
        return fail("ui.pages: at least one page is needed");
    }
    return out;
}

} // namespace

std::optional<std::string> check_pages(std::string_view json)
{
    std::string error;
    if (!parse(json, &error)) {
        return error;
    }
    return std::nullopt;
}

std::vector<std::string> pages(const Settings &settings)
{
    if (auto p = parse(settings.get("ui.pages"), nullptr)) {
        return *p;
    }
    return {kPages.begin(), kPages.end()};
}

std::string home_page(const std::vector<std::string> &pages)
{
    if (pages.empty() || std::find(pages.begin(), pages.end(), "eyes") != pages.end()) {
        return "eyes";
    }
    return pages.front();
}

} // namespace core
