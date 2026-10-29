// The built-in pages and the ui.pages setting: which of them to show, in swipe order.
// Mirrors host/internal/sim/pages.go (builtinPages, NVS.Pages, homePageLocked).
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace core {

class Settings;

// kPages are the page names ui.pages may hold. The firmware has no forecast, indoor and
// host page yet; it skips them.
constexpr std::array<std::string_view, 8> kPages = {"eyes",     "dashboard", "clock",     "forecast",
                                                    "indoor",   "host",      "countdown", "alarm"};

// check_pages validates a ui.pages value: a JSON list of known names, at least one, none
// twice. Returns the error, or nullopt.
std::optional<std::string> check_pages(std::string_view json);

// pages returns ui.pages; all pages if it is not valid.
std::vector<std::string> pages(const Settings &settings);

// home_page is where "swipe up" goes: the eyes, or the first page when they are hidden.
std::string home_page(const std::vector<std::string> &pages);

} // namespace core
