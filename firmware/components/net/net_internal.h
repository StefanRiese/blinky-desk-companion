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

} // namespace net
