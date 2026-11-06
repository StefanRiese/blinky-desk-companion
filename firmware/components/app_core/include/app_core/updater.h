// Updater: firmware updates over the device's own WiFi, from the GitHub releases. Once a
// day (and when ota.channel changes) it reads the first bytes of the board's app image
// (blinky-app-<board>.bin) for the channel in ota.channel and compares that image's version
// with the running one. A newer one is offered on the info screen and the web page; the
// web page's "Install" starts it, or with ota.auto on it installs by itself at night.
// Installing downloads the image into the other app partition and restarts; a new app that
// doesn't come up is rolled back by the bootloader (see app_main.cpp).
//
// HTTP, the download, the clock and the restart are hooks (esp_http_client and esp_ota_*
// on the device, fakes in the tests). run_due() runs in the network task and blocks while
// it checks or installs; check(), install() and info() are thread-safe. The version
// comparison mirrors firmware.Newer in host/internal/firmware/offers.go.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "app_core/settings.h"

namespace core {

// version_newer reports whether version a is newer than b, by semantic versioning: "0.2.0"
// > "0.2.0-rc1" > "0.1.3", "0.2.1-dev.27" > "0.2.1-dev.9"; "+build" is ignored. A version
// that doesn't parse (a bare commit) is never newer, and anything is newer than it.
bool version_newer(std::string_view a, std::string_view b);

// update_url is where the channel's app image for board is: "release" (the newest regular
// release) or "main" (the latest build from main); "" for "off" or an unknown channel.
std::string update_url(std::string_view channel, std::string_view board);

// image_version reads the version from the start of an ESP32 app image (its
// esp_app_desc_t); nullopt if head is no app image or too short.
std::optional<std::string> image_version(std::string_view head);

class Updater {
public:
    static constexpr size_t kHeadBytes = 256;               // enough for the app description
    static constexpr uint32_t kFirstCheckMs = 60 * 1000;    // after the internet comes up
    static constexpr uint32_t kCheckEveryMs = 24 * 3600 * 1000;
    static constexpr uint32_t kRetryMs = 3600 * 1000;       // after a failed check
    static constexpr int kAutoFrom = 3, kAutoTo = 5;        // ota.auto installs from 3:00 to 4:59

    enum class State { Off, Idle, Checking, Current, Available, Installing, Failed };

    struct Info {
        State state = State::Off;
        std::string version; // the offered (or installing) version
        std::string error;   // why the last check or install failed
        int percent = 0;     // while installing
    };

    struct Hooks {
        // head reads at least the first kHeadBytes of url (following redirects); a shorter
        // body is fine if the file is shorter. Returns the bytes or an error.
        std::function<std::optional<std::string>(const std::string &url, std::string &err)> head;
        // install downloads url into the other app partition and makes it the one to boot;
        // progress(percent) as it goes. Returns an error or nullopt.
        std::function<std::optional<std::string>(const std::string &url, const std::function<void(int)> &progress)>
            install;
        // busy: an update through a host link runs, so none starts here.
        std::function<bool()> busy;
        // local_hour is the hour of the local time (0 … 23), or -1 before the clock is set.
        std::function<int()> local_hour;
        // restart starts the new firmware.
        std::function<void()> restart;
    };

    Updater(Settings &settings, std::string fw, std::string board, Hooks hooks)
        : settings_(settings), fw_(std::move(fw)), board_(std::move(board)), hooks_(std::move(hooks))
    {
    }

    // run_due checks or installs when it's time (now in ms, wraps) and returns how long
    // until it wants to run again. Call it only while the internet is reachable.
    uint32_t run_due(uint32_t now);
    // check asks for a check at the next run_due (the web page's button, a new channel).
    void check();
    // install asks to install the offered version at the next run_due; false (with the
    // reason in err) if there is none or something else is updating.
    bool install(std::string &err);
    Info info() const;

    static const char *state_name(State s);

private:
    void set(State s, std::string version = "", std::string error = "");
    void do_check(uint32_t now);
    void do_install();

    Settings &settings_;
    const std::string fw_, board_;
    Hooks hooks_;
    mutable std::mutex mu_; // info_, check_asked_, install_asked_
    Info info_;
    bool check_asked_ = false, install_asked_ = false;
    bool started_ = false;
    uint32_t next_check_ = 0;
    std::string channel_; // the channel of the last check
    std::string url_;     // the offered image
};

} // namespace core
