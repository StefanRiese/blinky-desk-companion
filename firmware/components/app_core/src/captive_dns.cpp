#include "app_core/captive_dns.h"

namespace core {

std::string captive_dns_reply(std::string_view q, std::array<uint8_t, 4> ip)
{
    auto u8 = [&](size_t i) { return static_cast<uint8_t>(q[i]); };
    if (q.size() < 12 || (u8(2) & 0xF8) != 0 || u8(4) != 0 || u8(5) != 1) {
        return {}; // a response, not a standard query, or not exactly one question
    }
    size_t i = 12;
    while (i < q.size() && u8(i) != 0) {
        if ((u8(i) & 0xC0) != 0) {
            return {}; // compressed names don't occur in questions
        }
        i += 1 + u8(i);
    }
    if (i + 5 > q.size()) {
        return {};
    }
    const size_t end = i + 5; // the zero label, QTYPE, QCLASS
    const uint16_t qtype = static_cast<uint16_t>(u8(i + 1) << 8 | u8(i + 2));
    const uint16_t qclass = static_cast<uint16_t>(u8(i + 3) << 8 | u8(i + 4));
    const bool answer = (qtype == 1 || qtype == 255) && qclass == 1;

    std::string r(q.substr(0, end));
    r[2] = static_cast<char>(0x84 | (u8(2) & 0x01)); // response, authoritative, RD copied
    r[3] = 0;                                         // no error
    r[6] = 0;
    r[7] = answer ? 1 : 0; // answers
    r[8] = r[9] = r[10] = r[11] = 0;
    if (answer) {
        const char rr[] = {'\xC0', '\x0C', 0, 1, 0, 1, 0, 0, 0, 60, 0, 4}; // name → question, A, IN, TTL 60 s
        r.append(rr, sizeof rr);
        for (uint8_t b : ip) {
            r += static_cast<char>(b);
        }
    }
    return r;
}

} // namespace core
