// Time zones: the settings hold an IANA name (location.tz, e.g. "Europe/Berlin"); the C
// library needs a POSIX TZ rule. A small table for now; the full table follows in M4.
#pragma once

#include <optional>
#include <string_view>

namespace core {

// posix_tz returns the POSIX rule for an IANA name, or nullopt if it is not in the table.
std::optional<std::string_view> posix_tz(std::string_view iana);

} // namespace core
