// Blinky protocol v1 (protocol/PROTOCOL.md): NDJSON framing and the messages the device
// sends. Incoming messages are parsed by HostLink. JSON is built with ArduinoJson, so the
// output is compact and key order is fixed (the golden tests compare parsed JSON anyway).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace core::proto {

constexpr int kVersion = 1;
constexpr size_t kMaxLine = 2048; // bytes per line, without the newline

// LineReader splits a byte stream into lines. Lines that don't start with '{' (boot output,
// logs, noise) and lines longer than kMaxLine are dropped; a trailing '\r' is removed.
class LineReader {
public:
    using OnLine = std::function<void(std::string_view line)>;
    void feed(std::string_view bytes, const OnLine &on_line);

private:
    std::string buf_;
    bool overflow_ = false; // the current line is too long: skip it up to the newline
};

// Who the device is, for its hello.
struct DeviceInfo {
    std::string fw;
    std::string board;
    std::vector<std::string> caps;
};

// Messages from the device. Each returns one line without the newline. JSON parameters
// (p, values) are passed as JSON text and embedded as JSON.
std::string hello(std::string_view dev, const DeviceInfo &info, std::string_view token);
std::string sub(int id, std::string_view sid, std::string_view src, int every, std::string_view p_json);
std::string unsub(std::string_view sid);
std::string ack(int id);
std::string err(std::optional<int> id, std::string_view code, std::string_view msg);
std::string ping();
std::string log(char level, std::string_view tag, std::string_view msg);
std::string event(std::string_view kind, std::string_view name, std::string_view state);
std::string sensor(std::optional<double> temp, std::optional<double> hum);

// valid_sid checks a subscription id: [a-z0-9_]{1,16}.
bool valid_sid(std::string_view sid);

// same_json compares two JSON texts by content (key order and spacing don't matter).
bool same_json(std::string_view a, std::string_view b);

} // namespace core::proto
