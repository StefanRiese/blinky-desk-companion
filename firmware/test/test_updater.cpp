// Tests for core::Updater (firmware updates over WiFi from the GitHub releases), the version
// comparison it shares with buddy-link and reading the version from an app image.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "app_core/settings.h"
#include "app_core/updater.h"

using core::Updater;
using State = core::Updater::State;

namespace {

// app_head is the start of an app image with version v, as esp_app_desc_t lays it out.
std::string app_head(const std::string &v)
{
    std::string h(Updater::kHeadBytes, '\0');
    h[0] = static_cast<char>(0xE9);
    const uint32_t magic = 0xABCD5432;
    for (int i = 0; i < 4; i++) {
        h[0x20 + i] = static_cast<char>(magic >> (8 * i) & 0xFF);
    }
    h.replace(0x30, v.size(), v);
    return h;
}

struct Fixture {
    core::MemoryStorage storage;
    core::Settings settings{storage};
    std::string served = app_head("0.4.0");
    std::string head_err;
    std::vector<std::string> heads, installs;
    std::optional<std::string> install_err;
    bool busy = false, restarted = false;
    int hour = 12;
    Updater up{settings, "0.3.1", "amoled175",
               {.head =
                    [this](const std::string &url, std::string &err) -> std::optional<std::string> {
                        heads.push_back(url);
                        if (!head_err.empty()) {
                            err = head_err;
                            return std::nullopt;
                        }
                        return served;
                    },
                .install =
                    [this](const std::string &url, const std::function<void(int)> &progress) {
                        installs.push_back(url);
                        progress(50);
                        return install_err;
                    },
                .busy = [this] { return busy; },
                .local_hour = [this] { return hour; },
                .restart = [this] { restarted = true; }}};

    // checked runs the first check (a minute after the start).
    void checked()
    {
        up.run_due(0);
        up.run_due(Updater::kFirstCheckMs);
    }
};

} // namespace

TEST_CASE("version_newer: semantic versioning as firmware.Newer in buddy-link")
{
    struct Case {
        const char *a, *b;
        bool want;
    };
    for (const auto &c : std::vector<Case>{
             {"0.2.0", "0.1.3", true},
             {"0.2.0", "0.2.0-rc1", true},
             {"0.2.0-rc1", "0.2.0", false},
             {"0.2.1-dev.27+802b7bc", "0.2.1-dev.9+aaaaaaa", true},
             {"0.2.1-dev.9", "0.2.1-dev.27", false},
             {"0.2.1-dev.27", "0.2.0", true},
             {"0.2.0", "0.2.1-dev.27", false},
             {"v1.0.0", "0.9.9", true},
             {"0.2.0", "0.2.0", false},
             {"0.2.0", "7aff8b1", true}, // a bare commit is older than any version
             {"7aff8b1", "0.2.0", false},
         }) {
        CAPTURE(c.a);
        CAPTURE(c.b);
        CHECK(core::version_newer(c.a, c.b) == c.want);
    }
}

TEST_CASE("update_url: the board's app image per channel")
{
    CHECK(core::update_url("release", "amoled175") ==
          "https://github.com/StefanRiese/blinky-desk-companion/releases/latest/download/blinky-app-amoled175.bin");
    CHECK(core::update_url("main", "ws146") ==
          "https://github.com/StefanRiese/blinky-desk-companion/releases/download/latest-main/blinky-app-ws146.bin");
    CHECK(core::update_url("off", "ws146").empty());
}

TEST_CASE("image_version: from the app description at the start of the image")
{
    CHECK(core::image_version(app_head("0.3.2-dev.3+a8c765f")) == "0.3.2-dev.3+a8c765f");
    std::string not_app = app_head("0.3.1");
    not_app[0] = 0;
    CHECK_FALSE(core::image_version(not_app));
    CHECK_FALSE(core::image_version(app_head("0.3.1").substr(0, 0x40))); // too short
    std::string bad_magic = app_head("0.3.1");
    bad_magic[0x20] = 0;
    CHECK_FALSE(core::image_version(bad_magic));
}

TEST_CASE("Updater: first check a minute after the start, then once a day")
{
    Fixture f;
    CHECK(f.up.run_due(0) > 0);
    CHECK(f.heads.empty());
    CHECK(f.up.info().state == State::Idle);
    f.up.run_due(Updater::kFirstCheckMs);
    REQUIRE(f.heads.size() == 1);
    CHECK(f.heads[0] == core::update_url("release", "amoled175"));
    CHECK(f.up.info().state == State::Available);
    CHECK(f.up.info().version == "0.4.0");
    f.up.run_due(Updater::kFirstCheckMs + 3600 * 1000);
    CHECK(f.heads.size() == 1);
    f.up.run_due(Updater::kFirstCheckMs + Updater::kCheckEveryMs);
    CHECK(f.heads.size() == 2);
}

TEST_CASE("Updater: the same or an older version is current")
{
    Fixture f;
    f.served = app_head("0.3.1");
    f.checked();
    CHECK(f.up.info().state == State::Current);
    std::string err;
    CHECK_FALSE(f.up.install(err));
    CHECK(err == "no update to install");
}

TEST_CASE("Updater: a failed check is tried again after an hour")
{
    Fixture f;
    f.head_err = "request failed (ESP_ERR_HTTP_CONNECT)";
    f.checked();
    CHECK(f.up.info().state == State::Failed);
    CHECK(f.up.info().error == "check failed: request failed (ESP_ERR_HTTP_CONNECT)");
    f.head_err.clear();
    f.up.run_due(Updater::kFirstCheckMs + Updater::kRetryMs - 1);
    CHECK(f.heads.size() == 1);
    f.up.run_due(Updater::kFirstCheckMs + Updater::kRetryMs);
    CHECK(f.up.info().state == State::Available);

    Fixture g;
    g.served = "<html>not found</html>";
    g.checked();
    CHECK(g.up.info().error == "the download is no firmware image");
}

TEST_CASE("Updater: install downloads the offered image and restarts")
{
    Fixture f;
    f.checked();
    std::string err;
    REQUIRE(f.up.install(err));
    CHECK(f.installs.empty()); // in the network task, at the next run
    f.up.run_due(Updater::kFirstCheckMs + 1000);
    REQUIRE(f.installs.size() == 1);
    CHECK(f.installs[0] == core::update_url("release", "amoled175"));
    CHECK(f.restarted);
    CHECK(f.up.info().state == State::Installing);
    CHECK(f.up.info().percent == 100);
}

TEST_CASE("Updater: a failed install can be started again")
{
    Fixture f;
    f.checked();
    f.install_err = "esp_ota_write failed";
    std::string err;
    REQUIRE(f.up.install(err));
    f.up.run_due(Updater::kFirstCheckMs + 1000);
    CHECK_FALSE(f.restarted);
    CHECK(f.up.info().state == State::Failed);
    CHECK(f.up.info().version == "0.4.0");
    CHECK(f.up.info().error == "esp_ota_write failed");
    f.install_err.reset();
    REQUIRE(f.up.install(err));
    f.up.run_due(Updater::kFirstCheckMs + 2000);
    CHECK(f.restarted);
}

TEST_CASE("Updater: not while an update through a host link runs")
{
    Fixture f;
    f.checked();
    f.busy = true;
    std::string err;
    CHECK_FALSE(f.up.install(err));
    CHECK(err == "an update through a host link is running");
}

TEST_CASE("Updater: ota.auto installs by itself at night only")
{
    Fixture f;
    f.checked();
    CHECK(f.installs.empty()); // ota.auto is off by default
    REQUIRE_FALSE(f.settings.set("ota.auto", "true").has_value());
    f.up.run_due(Updater::kFirstCheckMs + 1000);
    CHECK(f.installs.empty()); // noon
    f.hour = 3;
    f.up.run_due(Updater::kFirstCheckMs + 2000);
    CHECK(f.installs.size() == 1);
    CHECK(f.restarted);
}

TEST_CASE("Updater: channels")
{
    Fixture f;
    f.checked();
    REQUIRE_FALSE(f.settings.set("ota.channel", "main").has_value());
    f.served = app_head("0.3.2-dev.3+a8c765f");
    f.up.run_due(Updater::kFirstCheckMs + 1000); // a new channel checks at once
    REQUIRE(f.heads.size() == 2);
    CHECK(f.heads[1] == core::update_url("main", "amoled175"));
    CHECK(f.up.info().version == "0.3.2-dev.3+a8c765f");

    REQUIRE_FALSE(f.settings.set("ota.channel", "off").has_value());
    f.up.run_due(Updater::kFirstCheckMs + 2000);
    CHECK(f.up.info().state == State::Off);
    std::string err;
    CHECK_FALSE(f.up.install(err));
    f.up.run_due(Updater::kFirstCheckMs + Updater::kCheckEveryMs * 2);
    CHECK(f.heads.size() == 2);
}

TEST_CASE("Updater: check asks for a check at the next run")
{
    Fixture f;
    f.checked();
    f.up.check();
    f.up.run_due(Updater::kFirstCheckMs + 1000);
    CHECK(f.heads.size() == 2);
}
