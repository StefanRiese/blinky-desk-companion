// Shared between the parts of the net component.
#pragma once

#include <cstddef>
#include <functional>

#include <ArduinoJson.h>

#include "app_core/settings.h"
#include "net/net.h"

namespace net {

core::Settings &settings();
const Hooks &hooks();
bool ap_on();

// start_web starts the web config page (web.cpp).
void start_web();

// start_updater creates the updater (update.cpp); run_updater runs it in the network task
// while the internet is reachable and returns how long it may wait.
void start_updater();
uint32_t run_updater(uint32_t now);

// The last crash (crash.cpp). crash_summary fills out and returns true if there is a core
// dump; crash_read hands the raw dump to out in pieces (false if there is none or out
// stopped); crash_erase removes it.
bool crash_summary(JsonObject out);
bool crash_read(const std::function<bool(const char *data, size_t n)> &out);
bool crash_erase();

} // namespace net
