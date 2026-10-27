// Settings as JSON, for every editor: config messages (host, setup page) and the web config
// page on the device. Values are typed as in the registry; secrets come back masked.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ArduinoJson.h>

#include "app_core/settings.h"

namespace core {

// settings_values adds the settings matching want ("*", a key, or a prefix such as "wifi")
// to values.
void settings_values(JsonObject values, const Settings &settings, std::string_view want);

// settings_schema lists the registry: {key, type, def, min?, max?, choices?, secret?} each.
void settings_schema(JsonArray out);

// apply_settings stores the values of a "set" object, all or none of them (see
// Settings::set_many). Returns the error, or nullopt and the stored keys in keys.
std::optional<std::string> apply_settings(Settings &settings, JsonObjectConst set, std::vector<std::string> &keys);

} // namespace core
