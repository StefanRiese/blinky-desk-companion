#include "app_core/settings_json.h"

namespace core {
namespace {

const char *type_name(Type t)
{
    switch (t) {
    case Type::Bool:
        return "bool";
    case Type::Int:
        return "int";
    case Type::Float:
        return "float";
    case Type::String:
        return "string";
    case Type::Enum:
        return "enum";
    case Type::Json:
        return "json";
    case Type::Color:
        return "color";
    }
    return "string";
}

void add_setting(JsonObject values, const Setting &s, const Settings &settings)
{
    const std::string key(s.key);
    switch (s.type) {
    case Type::Bool:
        values[key] = settings.flag(s.key);
        break;
    case Type::Int:
        values[key] = static_cast<long long>(settings.number(s.key));
        break;
    case Type::Float:
        values[key] = settings.number(s.key);
        break;
    case Type::Json: {
        JsonDocument tmp;
        const std::string v = settings.get(s.key);
        if (deserializeJson(tmp, v) == DeserializationError::Ok) {
            values[key] = tmp.as<JsonVariantConst>();
        } else {
            values[key] = v;
        }
        break;
    }
    case Type::String:
    case Type::Enum:
    case Type::Color:
        values[key] = settings.shown(s.key);
        break;
    }
}

void add_schema(JsonObject o, const Setting &s)
{
    o["key"] = s.key;
    o["type"] = type_name(s.type);
    o["def"] = s.def;
    if (s.min != s.max) {
        o["min"] = s.min;
        o["max"] = s.max;
    }
    if (!s.choices.empty()) {
        JsonArray c = o["choices"].to<JsonArray>();
        for (auto choice : s.choices) {
            c.add(choice);
        }
    }
    if (s.secret) {
        o["secret"] = true;
    }
}

} // namespace

void settings_values(JsonObject values, const Settings &settings, std::string_view want)
{
    for (const Setting &s : registry()) {
        const bool prefix = s.key.size() > want.size() && s.key.substr(0, want.size()) == want &&
                            s.key[want.size()] == '.';
        if (want == "*" || s.key == want || prefix) {
            add_setting(values, s, settings);
        }
    }
}

void settings_schema(JsonArray out)
{
    for (const Setting &s : registry()) {
        add_schema(out.add<JsonObject>(), s);
    }
}

size_t settings_schema_page(JsonArray out, size_t from, size_t budget)
{
    const auto all = registry();
    size_t used = 2; // []
    for (size_t i = from; i < all.size(); i++) {
        JsonDocument one;
        add_schema(one.to<JsonObject>(), all[i]);
        const size_t n = measureJson(one) + 1; // and a comma
        if (used + n > budget && out.size() > 0) {
            return i;
        }
        out.add(one.as<JsonObjectConst>());
        used += n;
    }
    return 0;
}

std::optional<std::string> apply_settings(Settings &settings, JsonObjectConst set, std::vector<std::string> &keys)
{
    std::vector<std::pair<std::string, std::string>> values;
    for (JsonPairConst kv : set) {
        std::string v;
        if (kv.value().is<const char *>()) {
            v = kv.value().as<const char *>();
        } else {
            serializeJson(kv.value(), v); // numbers, true/false, lists and objects as JSON text
        }
        values.emplace_back(std::string(kv.key().c_str(), kv.key().size()), std::move(v));
    }
    if (auto e = settings.set_many(values)) {
        return e;
    }
    keys.clear();
    for (const auto &kv : values) {
        keys.push_back(kv.first);
    }
    return std::nullopt;
}

} // namespace core
