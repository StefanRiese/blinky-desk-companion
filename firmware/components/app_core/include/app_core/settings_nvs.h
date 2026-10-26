// Settings storage in NVS (device only): the whole block as one blob, so key names are
// not limited to NVS's 15 characters and a change is written in one go.
#pragma once

#include "app_core/settings.h"

namespace core {

class NvsStorage : public Storage {
public:
    // init prepares the NVS partition (erases it if its format is outdated). Call once.
    static bool init();

    std::optional<std::string> load() override;
    bool save(std::string_view block) override;
};

} // namespace core
