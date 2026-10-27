// Captive portal DNS: while the setup access point is on, every name resolves to the device,
// so phones and laptops open the web config page by themselves.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace core {

// captive_dns_reply answers a DNS query: an A record with ip for IPv4 (A/ANY) questions, an
// empty answer for the rest (AAAA, …). Returns "" for anything that is not one standard
// query (responses, other opcodes, several questions, truncated packets).
std::string captive_dns_reply(std::string_view query, std::array<uint8_t, 4> ip);

} // namespace core
