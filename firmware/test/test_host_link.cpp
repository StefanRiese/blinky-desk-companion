#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <vector>

#include "app_core/host_link.h"
#include "app_core/protocol.h"
#include "app_core/source_router.h"

using core::HostLink;
namespace proto = core::proto;

namespace {

// golden reads protocol/examples/<name> (shared with the host's go tests).
std::vector<std::string> golden(const char *name)
{
    std::ifstream f(std::string(PROTOCOL_EXAMPLES) + "/" + name);
    REQUIRE(f.good());
    std::vector<std::string> lines;
    for (std::string l; std::getline(f, l);) {
        if (!l.empty()) {
            lines.push_back(l);
        }
    }
    return lines;
}

const proto::DeviceInfo kInfo{.fw = "0.3.0", .board = "s3", .caps = {"eyes", "touch", "sht40"}};

// Rig is a HostLink (as the USB link) with a SourceRouter, memory settings, a store and a
// recorded output.
struct Rig {
    uint32_t ms = 1000;
    core::MemoryStorage storage;
    core::Settings settings{storage};
    core::DataStore store{[this] { return ms; }};
    std::vector<std::string> out;
    std::vector<int64_t> times;
    std::vector<std::string> changed;
    std::vector<std::string> moods;
    HostLink link;
    core::SourceRouter router{settings, {&link}, nullptr};

    explicit Rig(const char *sources = nullptr)
        : link("usb", settings, store, kInfo,
               {.send = [this](std::string_view l) { out.emplace_back(l); },
                .set_time = [this](int64_t e) { times.push_back(e); },
                .changed =
                    [this](const std::vector<std::string> &keys) {
                        changed.insert(changed.end(), keys.begin(), keys.end());
                        router.route();
                    },
                .routing = [this] { router.route(); },
                .silent = nullptr,
                .mood =
                    [this](std::string_view m, int ttl) {
                        moods.emplace_back(std::string(m) + "/" + std::to_string(ttl));
                    }})
    {
        settings.set("dev", "buddy-a1b2c3");
        settings.set("location.auto", "false"); // the golden handshake has no location lookup
        if (sources) {
            settings.set("sources", sources);
        }
        router.route();
    }
    void in(std::string_view line) { link.on_line(line, ms); }
    void at(uint32_t t)
    {
        ms = t;
        link.tick(t);
    }
    // take returns the lines sent since the last call.
    std::vector<std::string> take()
    {
        auto o = std::move(out);
        out.clear();
        return o;
    }
};

const char *kGoldenSources = R"([{"sid":"time","src":"time","every":3600},)"
                             R"({"sid":"weather","src":"weather","every":900,"p":{"lat":51.2,"lon":6.8,"days":3}},)"
                             R"({"sid":"btc","src":"http","every":300,)"
                             R"("p":{"url":"https://api.example/price","pick":{"eur":"/bpi/EUR/rate_float"}}}])";

const char *kHostHello = R"({"t":"hello","v":1,"dev":"buddy-a1b2c3","host":"buddy-link@laptop","net":true,)"
                         R"("srcs":["time","weather","sys"]})";

} // namespace

TEST_CASE("LineReader frames NDJSON and drops everything else")
{
    proto::LineReader r;
    std::vector<std::string> lines;
    auto on = [&](std::string_view l) { lines.emplace_back(l); };
    r.feed("I (123) boot: hello\n{\"t\":\"pi", on);
    CHECK(lines.empty());
    r.feed("ng\"}\r\n\n{\"t\":\"a\"}\n", on);
    CHECK(lines == std::vector<std::string>{R"({"t":"ping"})", R"({"t":"a"})"});

    lines.clear();
    r.feed("{" + std::string(proto::kMaxLine, 'x') + "}\n{\"t\":\"ok\"}\n", on); // too long
    CHECK(lines == std::vector<std::string>{R"({"t":"ok"})"});
    r.feed("{" + std::string(proto::kMaxLine - 1, 'x') + "\n", on); // exactly the limit
    CHECK(lines.size() == 2);
}

TEST_CASE("protocol: every golden line parses, device messages match the encoders")
{
    const auto g = golden("handshake.ndjson");
    REQUIRE(g.size() == 26);
    for (const auto &l : g) {
        CHECK_MESSAGE(proto::same_json(l, l), l);
    }
    CHECK(proto::same_json(proto::hello("buddy-a1b2c3", kInfo, ""), g[0]));
    CHECK(proto::same_json(proto::sub(1, "time", "time", 3600, ""), g[2]));
    CHECK(proto::same_json(proto::sub(2, "weather", "weather", 900, R"({"lat":51.2,"lon":6.8,"days":3})"), g[5]));
    CHECK(proto::same_json(proto::unsub("weather"), g[13]));
    CHECK(proto::same_json(proto::sensor(22.4, 41.2), R"({"t":"sensor","temp":22.4,"hum":41.2})"));
    CHECK(proto::same_json(proto::event("touch", "swipe_left", ""), g[15]));
    CHECK(proto::same_json(proto::ping(), g[19]));
    CHECK(proto::same_json(proto::web("http://192.168.1.23/"), g[22]));
    CHECK(proto::same_json(proto::hello("d", kInfo, "secret"),
                           R"({"t":"hello","v":1,"dev":"d","fw":"0.3.0","board":"s3",)"
                           R"("caps":["eyes","touch","sht40"],"token":"secret"})"));
    CHECK_FALSE(proto::same_json(R"({"a":1})", R"({"a":2})"));
    CHECK(proto::valid_sid("ci_main2"));
    CHECK_FALSE(proto::valid_sid("CI"));
    CHECK_FALSE(proto::valid_sid("a-b"));
    CHECK_FALSE(proto::valid_sid("abcdefghijklmnopq"));
}

TEST_CASE("HostLink sends the settings schema in pages that fit one line")
{
    const auto g = golden("handshake.ndjson");
    Rig r;
    r.link.up(1000);
    r.in(kHostHello);
    r.take();

    r.in(g[20]); // schema_from 0
    std::vector<std::string> keys;
    int pages = 0;
    for (;;) {
        auto out = r.take();
        REQUIRE(out.size() == 1);
        CHECK(out[0].size() <= proto::kMaxLine);
        JsonDocument reply;
        deserializeJson(reply, out[0]);
        CHECK(reply["t"] == "config");
        CHECK(reply["id"] == 1001);
        for (JsonVariantConst e : reply["schema"].as<JsonArrayConst>()) {
            keys.emplace_back(e["key"].as<const char *>());
        }
        pages++;
        if (!reply["next"].is<int>()) {
            break;
        }
        REQUIRE(pages < 10);
        r.in(R"({"t":"config","id":1001,"schema_from":)" + std::to_string(reply["next"].as<int>()) + "}");
    }
    CHECK(pages > 1);
    REQUIRE(keys.size() == core::registry().size());
    for (size_t i = 0; i < keys.size(); i++) {
        CHECK(keys[i] == core::registry()[i].key);
    }

    r.in(R"({"t":"config","id":3,"schema_from":999})");
    CHECK(r.take() == std::vector<std::string>{R"({"t":"config","id":3,"schema":[]})"});
}

TEST_CASE("HostLink tells the host where the web page is")
{
    Rig r;
    r.link.set_web("http://192.168.1.23/", r.ms); // before the hello: kept for later
    r.link.up(1000);
    CHECK(r.take().size() == 1); // only the hello
    r.in(kHostHello);
    auto out = r.take();
    REQUIRE_FALSE(out.empty());
    CHECK(out.back() == R"({"t":"web","url":"http://192.168.1.23/"})");

    r.link.set_web("http://192.168.1.23/", r.ms); // unchanged: nothing
    CHECK(r.take().empty());
    r.link.set_web("", r.ms); // WiFi gone
    CHECK(r.take() == std::vector<std::string>{R"({"t":"web","url":""})"});

    // A setup page gets no web address.
    r.link.set_web("http://192.168.1.24/", r.ms);
    r.take();
    r.in(R"({"t":"hello","v":1,"dev":"buddy-a1b2c3","host":"setup page","role":"setup"})");
    CHECK(r.take().empty());
}

TEST_CASE("HostLink replays the golden handshake")
{
    const auto g = golden("handshake.ndjson");
    Rig r(kGoldenSources);
    auto subs = r.router.status();
    REQUIRE(subs.size() == 3);
    CHECK(subs[0].via == "none");
    CHECK(subs[0].msg == "WiFi has no internet, no host connected");

    r.link.up(r.ms);
    auto out = r.take();
    REQUIRE(out.size() == 1);
    CHECK(proto::same_json(out[0], g[0])); // device hello
    CHECK(r.link.state() == HostLink::State::Handshake);

    r.in(g[1]); // host hello, offers http as well
    CHECK(r.link.ready());
    CHECK(r.link.host_name() == "buddy-link@laptop");
    out = r.take();
    REQUIRE(out.size() == 3);
    CHECK(proto::same_json(out[0], g[2])); // sub time
    CHECK(proto::same_json(out[1], g[5])); // sub weather with its parameters
    CHECK(proto::same_json(out[2], g[8])); // sub btc (http)

    r.in(g[3]); // ack 1
    r.in(g[4]); // time
    CHECK(r.times == std::vector<int64_t>{1793000000});
    r.in(g[6]); // ack 2
    r.in(g[7]); // weather
    CHECK(r.store.number("weather.now.temp") == 11.5);
    CHECK(r.store.number("weather.now.code") == 61);
    CHECK(r.store.number("weather.rain_next_2h") == doctest::Approx(2.5));
    CHECK(r.store.get("weather.now.temp")->ttl_ms == 2700000);
    r.in(g[9]); // err 3: unknown_src

    subs = r.router.status();
    CHECK(subs[0].via == "host");
    CHECK(subs[0].link == "usb");
    CHECK(subs[0].state == "ok");
    CHECK(subs[1].state == "ok");
    CHECK(subs[2].state == "error");
    CHECK(subs[2].msg == R"(unknown_src: unknown source "http")");

    r.in(g[10]); // sub_state weather: error
    CHECK(r.router.status()[1].msg == "HTTP 503");
    CHECK(r.take().empty());

    r.in(g[11]); // host_state: no internet → weather moves away from the host
    out = r.take();
    REQUIRE(out.size() == 2);
    CHECK(proto::same_json(out[0], g[13])); // unsub weather
    CHECK(proto::same_json(out[1], R"({"t":"unsub","sid":"btc"})"));
    CHECK(r.router.status()[1].msg == "WiFi has no internet, host has no internet");

    r.in(R"({"t":"mood","mood":"happy","ttl":10})");
    r.in(R"({"t":"mood","mood":"sad"})");
    CHECK(r.moods == std::vector<std::string>{"happy/10", "sad/30"});

    r.in(g[12]); // data
    CHECK(r.store.number("sys.cpu") == 23);
    CHECK(r.store.get("sys.ram")->origin == core::Origin::Host);

    r.in(g[17]); // config get ui
    out = r.take();
    REQUIRE(out.size() == 1);
    JsonDocument reply;
    deserializeJson(reply, out[0]);
    CHECK(reply["t"] == "config");
    CHECK(reply["id"] == 1000);
    CHECK(reply["values"]["ui.brightness_day"] == 200);
    CHECK(reply["values"]["ui.pages"][0] == "eyes");
    CHECK_FALSE(reply["values"]["units.temp"].is<const char *>());

    r.at(r.ms + HostLink::kPingAfterMs);
    out = r.take();
    REQUIRE(out.size() == 1);
    CHECK(proto::same_json(out[0], g[19])); // ping
}

TEST_CASE("HostLink: hello without dev, retries, silence")
{
    Rig r;
    r.link.up(1000);
    r.take();
    // The host opens the port while our hello is still waiting: no second hello.
    r.in(R"({"t":"hello","v":1,"host":"buddy-link@laptop"})");
    CHECK(r.take().empty());
    // No answer for 5 s: hello again.
    r.at(1000 + HostLink::kHelloRetryMs);
    CHECK(r.take().size() == 1);
    // The host asks again while that hello is waiting, and once it is older than 5 s.
    r.ms = 8000;
    r.in(R"({"t":"hello","v":1,"host":"buddy-link@laptop"})");
    CHECK(r.take().empty());
    r.ms = 11500;
    r.in(R"({"t":"hello","v":1,"host":"buddy-link@laptop"})");
    CHECK(r.take().size() == 1);
    // A hello for another device is ignored.
    r.in(R"({"t":"hello","v":1,"dev":"other","srcs":["time"]})");
    CHECK(r.link.state() == HostLink::State::Handshake);
    // A wrong protocol version is refused.
    r.in(R"({"t":"hello","v":2,"dev":"buddy-a1b2c3"})");
    auto out = r.take();
    REQUIRE(out.size() == 1);
    CHECK(out[0].find(R"("code":"version")") != std::string::npos);

    r.in(kHostHello);
    CHECK(r.link.ready());
    auto subs = r.router.status(); // defaults: time, weather, aqi
    CHECK(subs[0].via == "host");
    CHECK(subs[1].via == "host");
    CHECK(subs[2].msg == "WiFi has no internet, host doesn't offer aqi");
    r.take();

    // Data keeps the link alive; 30 s of silence drops it and says hello again.
    r.at(30000);
    r.in(R"({"t":"ping"})");
    r.at(59000);
    CHECK(r.link.ready());
    r.take(); // pings
    r.at(60000);
    CHECK(r.link.state() == HostLink::State::Handshake);
    CHECK(r.router.status()[0].via == "none");
    out = r.take();
    REQUIRE(out.size() == 1);
    CHECK(out[0].find(R"("t":"hello")") != std::string::npos);

    r.link.down();
    CHECK(r.link.state() == HostLink::State::Off);
    r.in(kHostHello);
    CHECK(r.link.state() == HostLink::State::Off);
}

TEST_CASE("HostLink: the setup page only reads and writes settings")
{
    Rig r;
    r.settings.set("link.token", "s3cret");
    r.settings.set("wifi.pass", "hunter2");
    r.link.up(1000);
    CHECK(r.take()[0].find("s3cret") != std::string::npos); // the host gets the token
    r.in(kHostHello);
    CHECK(r.take().size() == 2); // time, weather

    // The setup page takes the port over (buddy-link released it): no token for the page.
    r.at(20000);
    CHECK(r.take() == std::vector<std::string>{R"({"t":"ping"})"});
    r.in(R"({"t":"hello","v":1,"host":"setup page","role":"setup"})");
    auto out = r.take();
    REQUIRE(out.size() == 1);
    CHECK(out[0].find("token") == std::string::npos);
    CHECK(r.router.status()[0].via == "none");
    r.in(R"({"t":"hello","v":1,"dev":"buddy-a1b2c3","host":"setup page","role":"setup"})");
    CHECK(r.link.ready());
    CHECK(r.link.peer() == HostLink::Peer::Setup);
    CHECK(r.take().empty()); // no subscriptions

    r.in(R"({"t":"data","kv":{"sys.cpu":5}})");
    CHECK_FALSE(r.store.get("sys.cpu"));
    r.link.send_event("touch", "tap");
    CHECK(r.take().empty());

    r.in(R"({"t":"config","id":7,"get":["wifi","location.tz"]})");
    out = r.take();
    REQUIRE(out.size() == 1);
    CHECK(proto::same_json(out[0], R"({"t":"config","id":7,"values":{"wifi.ssid":"","wifi.pass":"***",)"
                                   R"("location.tz":"Europe/Berlin"}})"));

    // All or nothing: one bad value rejects the whole set.
    r.in(R"({"t":"config","id":8,"set":{"wifi.ssid":"Home","ui.brightness_day":300}})");
    out = r.take();
    REQUIRE(out.size() == 1);
    CHECK(proto::same_json(out[0], R"({"t":"err","id":8,"code":"bad_value",)"
                                   R"("msg":"ui.brightness_day must be from 0 to 255"})"));
    CHECK(r.settings.get("wifi.ssid").empty());

    r.in(R"({"t":"config","id":9,"set":{"wifi.ssid":"Home","wifi.pass":"***","ui.brightness_day":180,)"
         R"("units.temp":"F","ui.pages":["eyes","alarm"],"sound.enabled":false}})");
    CHECK(r.take() == std::vector<std::string>{R"({"t":"ack","id":9})"});
    CHECK(r.settings.get("wifi.ssid") == "Home");
    CHECK(r.settings.get("wifi.pass") == "hunter2"); // the mask keeps the secret
    CHECK(r.settings.get("ui.brightness_day") == "180");
    CHECK(r.settings.get("ui.pages") == R"(["eyes","alarm"])");
    CHECK(r.settings.get("sound.enabled") == "false");
    CHECK(r.changed.size() == 6);
}

TEST_CASE("HostLink: config changes move subscriptions and re-introduce the device")
{
    Rig r;
    r.link.up(1000);
    r.in(kHostHello);
    r.take();

    // A new location changes the weather parameters: unsub + sub with a new id.
    r.in(R"({"t":"config","set":{"location.lat":48.1,"location.lon":11.6}})");
    auto out = r.take();
    REQUIRE(out.size() == 2);
    CHECK(proto::same_json(out[0], R"({"t":"unsub","sid":"weather"})"));
    CHECK(proto::same_json(out[1], R"({"t":"sub","id":3,"sid":"weather","src":"weather","every":900,)"
                                   R"("p":{"lat":48.1,"lon":11.6}})"));

    // Sources: sys added, aqi removed (it was never subscribed), weather unchanged.
    r.in(R"({"t":"config","set":{"sources":[{"sid":"time","src":"time","every":3600},)"
         R"({"sid":"weather","src":"weather","every":900},{"sid":"sys","src":"sys","every":10}]}})");
    out = r.take();
    REQUIRE(out.size() == 1);
    CHECK(proto::same_json(out[0], R"({"t":"sub","id":4,"sid":"sys","src":"sys","every":10})"));
    r.in(R"({"t":"ack","id":4,"every":15})");
    CHECK(r.router.status()[2].every == 15);
    r.in(R"({"t":"data","ttl":20,"kv":{"sys.cpu":12,"sys.host":"laptop","user.coffee":true},"del":["x.y"]})");
    CHECK(r.router.status()[2].state == "ok");
    CHECK(r.store.text("sys.host") == "laptop");
    CHECK(r.store.get("user.coffee")->origin == core::Origin::Push);

    // via: device waits for WiFi.
    r.in(R"({"t":"config","set":{"sources":[{"sid":"time","src":"time","via":"device"}]}})");
    out = r.take();
    REQUIRE(out.size() == 3); // unsub time, weather, sys
    CHECK(r.router.status().size() == 1);
    CHECK(r.router.status()[0].via == "none");
    CHECK(r.router.status()[0].msg == "WiFi has no internet");

    // A new device name: say hello again under the new name.
    r.in(R"({"t":"config","set":{"dev":"desk"}})");
    out = r.take();
    REQUIRE(out.size() == 1);
    CHECK(out[0].find(R"("dev":"desk")") != std::string::npos);
    CHECK(r.link.state() == HostLink::State::Handshake);

    r.in(R"({"t":"hello","v":1,"dev":"desk","srcs":["time"]})");
    CHECK(r.link.ready());
    r.link.send_event("countdown", "", "done");
    CHECK(r.take() == std::vector<std::string>{R"({"t":"event","kind":"countdown","state":"done"})"});

    // auth errors are kept for the status display.
    r.in(R"({"t":"err","code":"auth","msg":"token missing"})");
    CHECK(r.link.error() == "auth: token missing");
}

TEST_CASE("HostLink passes ota messages from a host to the hook, not from the setup page")
{
    Rig r;
    std::vector<std::string> got;
    // A second link with an ota hook (the Rig's link has none).
    HostLink link("usb", r.settings, r.store, kInfo,
                  {.send = [&](std::string_view l) { r.out.emplace_back(l); },
                   .ota = [&](JsonVariantConst m) {
                       got.push_back(m["t"].as<std::string>());
                       return std::string(R"({"t":"ack","id":7})");
                   }});
    link.up(1000);
    link.on_line(kHostHello, 1000);
    r.take();
    link.on_line(R"({"t":"ota","id":7,"abort":true})", 1000);
    CHECK(got.size() == 1);
    CHECK(r.take() == std::vector<std::string>{R"({"t":"ack","id":7})"});

    // Without a hook the device says it takes no updates.
    r.link.up(1000);
    r.in(kHostHello);
    r.take();
    r.in(R"({"t":"ota","id":8,"abort":true})");
    const auto out = r.take();
    REQUIRE(out.size() == 1);
    CHECK(out[0].find("takes no updates") != std::string::npos);
}

TEST_CASE("HostLink passes notifications on")
{
    Rig r;
    std::vector<core::Notification> got;
    HostLink link("usb", r.settings, r.store, kInfo,
                  {.send = [&](std::string_view l) { r.out.emplace_back(l); },
                   .notify = [&](const core::Notification &n) { got.push_back(n); }});
    link.up(1000);
    link.on_line(kHostHello, 1000);
    link.on_line(R"({"t":"notify","text":"Pipeline failed: main","icon":"ci_fail","mood":"sad","sound":"beep","dur":5})", 1000);
    link.on_line(R"({"t":"notify","text":"hi"})", 1000);
    link.on_line(R"({"t":"notify","text":""})", 1000); // nothing to show
    REQUIRE(got.size() == 2);
    CHECK(got[0].text == "Pipeline failed: main");
    CHECK(got[0].mood == "sad");
    CHECK(got[0].dur == 5);
    CHECK(got[1].dur == 8); // the default
}
