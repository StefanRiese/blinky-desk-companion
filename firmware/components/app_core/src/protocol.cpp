#include "app_core/protocol.h"

#include <ArduinoJson.h>

namespace core::proto {
namespace {

std::string line(const JsonDocument &doc)
{
    std::string out;
    serializeJson(doc, out);
    return out;
}

// embed parses JSON text into doc[key]; text that isn't JSON is left out.
void embed(JsonDocument &doc, const char *key, std::string_view json)
{
    if (json.empty()) {
        return;
    }
    JsonDocument tmp;
    if (deserializeJson(tmp, json.data(), json.size()) == DeserializationError::Ok) {
        doc[key] = tmp.as<JsonVariantConst>();
    }
}

JsonString str(std::string_view s)
{
    return JsonString(s.data(), s.size());
}

} // namespace

void LineReader::feed(std::string_view bytes, const OnLine &on_line)
{
    for (char c : bytes) {
        if (c != '\n') {
            if (overflow_) {
                continue;
            }
            if (buf_.size() >= kMaxLine + 1) { // + 1 for a '\r' before the newline
                overflow_ = true;
                buf_.clear();
                continue;
            }
            buf_ += c;
            continue;
        }
        if (!buf_.empty() && buf_.back() == '\r') {
            buf_.pop_back();
        }
        if (!overflow_ && !buf_.empty() && buf_.front() == '{' && buf_.size() <= kMaxLine) {
            on_line(buf_);
        }
        buf_.clear();
        overflow_ = false;
    }
}

std::string hello(std::string_view dev, const DeviceInfo &info, std::string_view token)
{
    JsonDocument doc;
    doc["t"] = "hello";
    doc["v"] = kVersion;
    doc["dev"] = str(dev);
    doc["fw"] = info.fw;
    doc["board"] = info.board;
    JsonArray caps = doc["caps"].to<JsonArray>();
    for (const auto &c : info.caps) {
        caps.add(c);
    }
    if (!token.empty()) {
        doc["token"] = str(token);
    }
    return line(doc);
}

std::string sub(int id, std::string_view sid, std::string_view src, int every, std::string_view p_json)
{
    JsonDocument doc;
    doc["t"] = "sub";
    doc["id"] = id;
    doc["sid"] = str(sid);
    doc["src"] = str(src);
    if (every > 0) {
        doc["every"] = every;
    }
    embed(doc, "p", p_json);
    return line(doc);
}

std::string unsub(std::string_view sid)
{
    JsonDocument doc;
    doc["t"] = "unsub";
    doc["sid"] = str(sid);
    return line(doc);
}

std::string ack(int id)
{
    JsonDocument doc;
    doc["t"] = "ack";
    doc["id"] = id;
    return line(doc);
}

std::string err(std::optional<int> id, std::string_view code, std::string_view msg)
{
    JsonDocument doc;
    doc["t"] = "err";
    if (id) {
        doc["id"] = *id;
    }
    doc["code"] = str(code);
    doc["msg"] = str(msg);
    return line(doc);
}

std::string ping()
{
    return R"({"t":"ping"})";
}

std::string log(char level, std::string_view tag, std::string_view msg)
{
    JsonDocument doc;
    doc["t"] = "log";
    doc["lvl"] = std::string(1, level);
    doc["tag"] = str(tag);
    doc["msg"] = str(msg);
    return line(doc);
}

std::string event(std::string_view kind, std::string_view name, std::string_view state)
{
    JsonDocument doc;
    doc["t"] = "event";
    doc["kind"] = str(kind);
    if (!name.empty()) {
        doc["name"] = str(name);
    }
    if (!state.empty()) {
        doc["state"] = str(state);
    }
    return line(doc);
}

std::string web(std::string_view url)
{
    JsonDocument doc;
    doc["t"] = "web";
    doc["url"] = str(url);
    return line(doc);
}

std::string sensor(std::optional<double> temp, std::optional<double> hum, std::optional<double> noise)
{
    JsonDocument doc;
    doc["t"] = "sensor";
    if (temp) {
        doc["temp"] = *temp;
    }
    if (hum) {
        doc["hum"] = *hum;
    }
    if (noise) {
        doc["noise"] = *noise;
    }
    return line(doc);
}

bool valid_sid(std::string_view sid)
{
    if (sid.empty() || sid.size() > 16) {
        return false;
    }
    for (char c : sid) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

bool same_json(std::string_view a, std::string_view b)
{
    JsonDocument da, db;
    if (deserializeJson(da, a.data(), a.size()) != DeserializationError::Ok ||
        deserializeJson(db, b.data(), b.size()) != DeserializationError::Ok) {
        return false;
    }
    return da.as<JsonVariantConst>() == db.as<JsonVariantConst>();
}

} // namespace core::proto
