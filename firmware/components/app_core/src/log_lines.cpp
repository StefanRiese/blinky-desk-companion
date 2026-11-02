// ESP log output as whole lines (see log_lines.h).
#include "app_core/log_lines.h"

#include <cstring>

namespace core {

void LogLines::feed(std::string_view text, const OnLine &on_line)
{
    for (char c : text) {
        if (c == '\n') {
            flush(on_line);
            continue;
        }
        buf_ += c;
        if (buf_.size() >= kMaxLine) {
            flush(on_line);
        }
    }
}

void LogLines::flush(const OnLine &on_line)
{
    while (!buf_.empty() && buf_.back() == '\r') {
        buf_.pop_back();
    }
    if (!buf_.empty()) {
        on_line(buf_);
    }
    buf_.clear();
}

std::optional<LogLine> parse_log_line(std::string_view line)
{
    // Colour codes ("\033[0;32m" … "\033[0m") can sit anywhere once two calls are joined.
    std::string s;
    s.reserve(line.size());
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '\033') {
            const size_t m = line.find('m', i);
            if (m == std::string_view::npos) {
                break;
            }
            i = m;
            continue;
        }
        if (line[i] != '\r' && line[i] != '\n') {
            s += line[i];
        }
    }
    while (!s.empty() && s.back() == ' ') {
        s.pop_back();
    }
    if (s.empty()) {
        return std::nullopt;
    }
    LogLine out;
    out.msg = s;
    if (s.size() > 4 && std::strchr("EWIDV", s[0]) != nullptr && s[1] == ' ' && s[2] == '(') {
        out.level = s[0];
        const size_t close = s.find(") ");
        if (close == std::string::npos) {
            return out;
        }
        const size_t start = close + 2;
        const size_t colon = s.find(':', start);
        const size_t space = s.find(' ', start);
        if (colon != std::string::npos && colon > start && (space == std::string::npos || colon < space)) {
            out.tag = s.substr(start, colon - start);
            size_t m = colon + 1;
            while (m < s.size() && s[m] == ' ') {
                m++;
            }
            out.msg = s.substr(m);
        } else {
            out.msg = s.substr(start);
        }
    }
    return out;
}

} // namespace core
