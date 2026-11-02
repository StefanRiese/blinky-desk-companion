// ESP log output as whole lines, split into level, tag and message. Some ESP-IDF parts print
// one line in two calls (the WiFi driver: "I (5758) wifi:", then the text), so the serial link
// collects the output up to the newline before it sends a `log` message to the host.
// Portable and tested on the desktop; serial_link adds the locking.
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace core {

// LogLines collects log output and hands out finished lines.
class LogLines {
public:
    static constexpr size_t kMaxLine = 512; // a longer line is handed out in pieces
    using OnLine = std::function<void(std::string_view line)>;

    // feed adds output. Each finished line goes to on_line without '\n' or '\r'; empty lines
    // are skipped.
    void feed(std::string_view text, const OnLine &on_line);
    // flush hands out what there is of an unfinished line.
    void flush(const OnLine &on_line);
    bool pending() const { return !buf_.empty(); }

private:
    std::string buf_;
};

struct LogLine {
    char level = 'I';
    std::string tag;
    std::string msg;
};

// parse_log_line removes colour codes and reads "I (1234) tag: message" (also "tag:message").
// A line in another shape is all message with level I. nullopt if nothing is left.
std::optional<LogLine> parse_log_line(std::string_view line);

} // namespace core
