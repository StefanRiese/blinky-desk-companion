// Updater: checks the GitHub releases for newer firmware and installs it over WiFi (see
// updater.h). The version comparison mirrors host/internal/firmware/offers.go.
#include "app_core/updater.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <vector>

namespace core {
namespace {

constexpr const char *kReleases = "https://github.com/StefanRiese/blinky-desk-companion/releases/";
constexpr uint32_t kIdleMs = 60 * 1000; // how often run_due looks again without a reason

bool reached(uint32_t now, uint32_t at)
{
    return static_cast<int32_t>(now - at) >= 0;
}

struct Version {
    int nums[3] = {};
    std::vector<std::string_view> pre; // dot-separated pre-release identifiers; none for a release
};

std::optional<int> number(std::string_view s)
{
    int n = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (ec != std::errc() || end != s.data() + s.size() || s.empty() || n < 0) {
        return std::nullopt;
    }
    return n;
}

std::vector<std::string_view> split(std::string_view s, char sep)
{
    std::vector<std::string_view> out;
    for (;;) {
        const size_t i = s.find(sep);
        out.push_back(s.substr(0, i));
        if (i == std::string_view::npos) {
            return out;
        }
        s.remove_prefix(i + 1);
    }
}

std::optional<Version> parse_version(std::string_view v)
{
    if (v.starts_with('v')) {
        v.remove_prefix(1);
    }
    v = v.substr(0, v.find('+'));
    const size_t dash = v.find('-');
    const auto parts = split(v.substr(0, dash), '.');
    if (parts.size() != 3) {
        return std::nullopt;
    }
    Version out;
    for (size_t i = 0; i < 3; i++) {
        const auto n = number(parts[i]);
        if (!n) {
            return std::nullopt;
        }
        out.nums[i] = *n;
    }
    if (dash != std::string_view::npos) {
        out.pre = split(v.substr(dash + 1), '.');
    }
    return out;
}

// compare_pre orders pre-release identifiers: none (a release) > any; numbers numerically,
// below text; a longer list wins a tie.
int compare_pre(const std::vector<std::string_view> &a, const std::vector<std::string_view> &b)
{
    if (a.empty() || b.empty()) {
        return static_cast<int>(b.size() > 0) - static_cast<int>(a.size() > 0);
    }
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
        const auto na = number(a[i]), nb = number(b[i]);
        if (na && nb && *na != *nb) {
            return *na > *nb ? 1 : -1;
        }
        if (na && !nb) {
            return -1;
        }
        if (!na && nb) {
            return 1;
        }
        if (!na && !nb && a[i] != b[i]) {
            return a[i] > b[i] ? 1 : -1;
        }
    }
    return static_cast<int>(a.size()) - static_cast<int>(b.size());
}

uint32_t le32(std::string_view s, size_t at)
{
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) {
        v = v << 8 | static_cast<uint8_t>(s[at + static_cast<size_t>(i)]);
    }
    return v;
}

} // namespace

bool version_newer(std::string_view a, std::string_view b)
{
    const auto x = parse_version(a);
    const auto y = parse_version(b);
    if (!x) {
        return false;
    }
    if (!y) {
        return true;
    }
    for (int i = 0; i < 3; i++) {
        if (x->nums[i] != y->nums[i]) {
            return x->nums[i] > y->nums[i];
        }
    }
    return compare_pre(x->pre, y->pre) > 0;
}

std::string update_url(std::string_view channel, std::string_view board)
{
    const std::string file = "blinky-app-" + std::string(board) + ".bin";
    if (channel == "release") {
        return std::string(kReleases) + "latest/download/" + file; // GitHub redirects to the newest release
    }
    if (channel == "main") {
        return std::string(kReleases) + "download/latest-main/" + file;
    }
    return "";
}

std::optional<std::string> image_version(std::string_view head)
{
    // An app image starts with 0xE9; its first segment's data is the esp_app_desc_t (magic
    // word 0xABCD5432 at 0x20, the version as 32 chars at 0x30), as firmware.Describe reads it.
    constexpr size_t kDesc = 0x20, kVersion = kDesc + 16, kVersionLen = 32;
    if (head.size() < kVersion + kVersionLen || static_cast<uint8_t>(head[0]) != 0xE9 ||
        le32(head, kDesc) != 0xABCD5432) {
        return std::nullopt;
    }
    std::string_view v = head.substr(kVersion, kVersionLen);
    v = v.substr(0, v.find('\0'));
    if (v.empty()) {
        return std::nullopt;
    }
    return std::string(v);
}

const char *Updater::state_name(State s)
{
    switch (s) {
    case State::Off:
        return "off";
    case State::Idle:
        return "idle";
    case State::Checking:
        return "checking";
    case State::Current:
        return "current";
    case State::Available:
        return "available";
    case State::Installing:
        return "installing";
    case State::Failed:
        return "failed";
    }
    return "off";
}

void Updater::set(State s, std::string version, std::string error)
{
    std::lock_guard lock(mu_);
    info_ = {.state = s, .version = std::move(version), .error = std::move(error), .percent = 0};
}

Updater::Info Updater::info() const
{
    std::lock_guard lock(mu_);
    return info_;
}

void Updater::check()
{
    std::lock_guard lock(mu_);
    check_asked_ = true;
}

bool Updater::install(std::string &err)
{
    std::lock_guard lock(mu_);
    if (info_.state == State::Installing) {
        err = "an update is already being installed";
        return false;
    }
    if (url_.empty() || (info_.state != State::Available && info_.state != State::Failed)) {
        err = "no update to install";
        return false;
    }
    if (hooks_.busy && hooks_.busy()) {
        err = "an update through a host link is running";
        return false;
    }
    install_asked_ = true;
    return true;
}

uint32_t Updater::run_due(uint32_t now)
{
    const std::string channel = settings_.get("ota.channel");
    const std::string url = update_url(channel, board_);
    bool check_now = false, install_now = false;
    {
        std::lock_guard lock(mu_);
        if (url.empty()) {
            info_ = {.state = State::Off, .version = "", .error = "", .percent = 0};
            url_.clear();
            check_asked_ = install_asked_ = false;
            channel_ = channel;
            return kIdleMs;
        }
        if (info_.state == State::Off) {
            info_ = {.state = State::Idle, .version = "", .error = "", .percent = 0};
        }
        std::swap(check_now, check_asked_);
        std::swap(install_now, install_asked_);
    }
    if (!started_) {
        started_ = true;
        next_check_ = now + kFirstCheckMs;
    } else if (channel != channel_) {
        check_now = true; // a new channel offers other builds
        std::lock_guard lock(mu_);
        url_.clear();
        info_ = {.state = State::Idle, .version = "", .error = "", .percent = 0};
    }
    channel_ = channel;

    if (install_now) {
        do_install();
        return kIdleMs;
    }
    if (check_now || reached(now, next_check_)) {
        do_check(now);
    }
    const Info i = info();
    if (i.state == State::Available && settings_.get("ota.auto") == "true" && hooks_.local_hour) {
        const int h = hooks_.local_hour();
        if (h >= kAutoFrom && h < kAutoTo && !(hooks_.busy && hooks_.busy())) {
            do_install();
        }
    }
    return std::min(kIdleMs, next_check_ - now);
}

void Updater::do_check(uint32_t now)
{
    const std::string url = update_url(channel_, board_);
    set(State::Checking);
    std::string err;
    const auto head = hooks_.head ? hooks_.head(url, err) : std::nullopt;
    const auto version = head ? image_version(*head) : std::nullopt;
    if (!version) {
        set(State::Failed, "", head ? "the download is no firmware image" : "check failed: " + err);
        std::lock_guard lock(mu_);
        url_.clear();
        next_check_ = now + kRetryMs;
        return;
    }
    next_check_ = now + kCheckEveryMs;
    if (version_newer(*version, fw_)) {
        set(State::Available, *version);
        std::lock_guard lock(mu_);
        url_ = url;
    } else {
        set(State::Current, *version);
        std::lock_guard lock(mu_);
        url_.clear();
    }
}

void Updater::do_install()
{
    std::string url, version;
    {
        std::lock_guard lock(mu_);
        url = url_;
        version = info_.version;
        info_ = {.state = State::Installing, .version = version, .error = "", .percent = 0};
    }
    if (hooks_.busy && hooks_.busy()) {
        set(State::Failed, version, "an update through a host link is running");
        return;
    }
    const auto err = hooks_.install ? hooks_.install(url, [this](int percent) {
        std::lock_guard lock(mu_);
        info_.percent = percent;
    })
                                    : std::optional<std::string>("no installer");
    if (err) {
        set(State::Failed, version, *err);
        return;
    }
    {
        std::lock_guard lock(mu_);
        info_.percent = 100;
    }
    if (hooks_.restart) {
        hooks_.restart();
    }
}

} // namespace core
