// Shared between the parts of the net component.
#pragma once

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

} // namespace net
