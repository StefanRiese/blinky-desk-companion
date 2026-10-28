// SourceRouter with two host links and the device's own fetcher, the DeviceFetcher with a
// fake HTTP client, and the small network helpers (Improv Serial, captive DNS, host:port).
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "app_core/captive_dns.h"
#include "app_core/device_fetcher.h"
#include "app_core/host_link.h"
#include "app_core/improv.h"
#include "app_core/settings_json.h"
#include "app_core/source_router.h"
#include "app_core/status.h"

using core::HostLink;
namespace fetch = core::fetch;

namespace {

const core::proto::DeviceInfo kInfo{.fw = "0.3.0", .board = "s3", .caps = {"eyes"}};

const char *kForecast = R"({"latitude":51.22,"current":{"time":"2026-10-26T07:15","temperature_2m":8.44,)"
                        R"("apparent_temperature":6.1,"relative_humidity_2m":82,"weather_code":3,)"
                        R"("wind_speed_10m":14.04,"is_day":1},"minutely_15":{"precipitation":[0,0.1,null,0.3]}})";

// Net is the firmware's arrangement: USB and TCP links, the device fetcher, one router.
struct Net {
    uint32_t ms = 1000;
    core::MemoryStorage storage;
    core::Settings settings{storage};
    core::DataStore store{[this] { return ms; }};
    std::vector<std::string> usb_out, tcp_out, urls;
    std::vector<int64_t> times;
    fetch::Response answer{.status = 200, .body = kForecast, .error = ""};
    core::DeviceFetcher device{store, [this] { return ms; },
                               {.get =
                                    [this](const std::string &url, const fetch::Headers &) {
                                        urls.push_back(url);
                                        return answer;
                                    },
                                .sync_time = [] { return std::optional<int64_t>(1793000000); },
                                .set_time = [this](int64_t e) { times.push_back(e); }}};
    HostLink usb{"usb", settings, store, kInfo, hooks(usb_out)};
    HostLink tcp{"tcp", settings, store, kInfo, hooks(tcp_out)};
    core::SourceRouter router{settings, {&usb, &tcp}, &device};

    Net()
    {
        settings.set("dev", "d");
        router.route();
    }
    HostLink::Hooks hooks(std::vector<std::string> &out)
    {
        return {.send = [&out](std::string_view l) { out.emplace_back(l); },
                .set_time = nullptr,
                .changed = [this](const std::vector<std::string> &) { router.route(); },
                .routing = [this] { router.route(); },
                .silent = nullptr,
                .mood = nullptr};
    }
    static void connect(HostLink &l, const char *srcs, bool net = true)
    {
        l.up(1000);
        l.on_line(std::string(R"({"t":"hello","v":1,"dev":"d","host":"h","net":)") + (net ? "true" : "false") +
                      R"(,"srcs":)" + srcs + "}",
                  1000);
    }
    static std::vector<std::string> take(std::vector<std::string> &v)
    {
        auto o = std::move(v);
        v.clear();
        return o;
    }
    std::string via(size_t i) const
    {
        const auto s = router.status()[i];
        return s.via == "host" ? s.link : s.via;
    }
};

} // namespace

TEST_CASE("SourceRouter: USB before TCP, the device first when its WiFi has internet")
{
    Net n; // time, weather, aqi
    CHECK(n.via(0) == "none");

    Net::connect(n.tcp, R"(["time","weather","aqi","sys"])");
    CHECK(n.via(0) == "tcp");
    CHECK(n.via(2) == "tcp");
    CHECK(Net::take(n.tcp_out).size() == 4); // hello, 3 subs

    // USB comes up: it is preferred for what it offers; the rest stays on TCP.
    Net::connect(n.usb, R"(["time","weather"])");
    CHECK(n.via(0) == "usb");
    CHECK(n.via(1) == "usb");
    CHECK(n.via(2) == "tcp");
    CHECK(Net::take(n.tcp_out) ==
          std::vector<std::string>{R"({"t":"unsub","sid":"time"})", R"({"t":"unsub","sid":"weather"})"});
    CHECK(Net::take(n.usb_out).size() == 3); // hello, time, weather

    // WiFi with internet: the device fetches the native sources, the hosts let go.
    n.router.set_device_net(true);
    CHECK(n.via(0) == "device");
    CHECK(n.via(2) == "device");
    CHECK(Net::take(n.usb_out).size() == 2);
    CHECK(Net::take(n.tcp_out).size() == 1);
    CHECK(n.router.status()[1].state == "pending");
    n.device.run_due();
    CHECK(n.times == std::vector<int64_t>{1793000000});
    CHECK(n.store.number("weather.now.temp") == doctest::Approx(8.4));
    CHECK(n.store.get("weather.now.temp")->origin == core::Origin::Device);
    CHECK(n.store.get("weather.now.temp")->ttl_ms == 2700000); // three intervals
    CHECK(n.store.number("weather.rain_next_2h") == doctest::Approx(0.4));
    CHECK(n.router.status()[1].state == "ok");
    // aqi got the forecast answer as well: not what Open-Meteo's air quality API says.
    CHECK(n.router.status()[2].state == "error");
    CHECK(n.router.status()[2].msg == "unexpected answer from Open-Meteo");

    // Internet gone: back to the hosts.
    n.router.set_device_net(false);
    CHECK(n.via(0) == "usb");
    CHECK(n.via(2) == "tcp");

    // USB unplugged: TCP takes over; forced sources stay where they are told to.
    n.usb.down();
    CHECK(n.via(0) == "tcp");
    n.settings.set("sources", R"([{"sid":"w","src":"weather","via":"device"},{"sid":"s","src":"sys","via":"host"},)"
                              R"({"sid":"g","src":"gitlab"}])");
    n.router.route();
    CHECK(n.via(0) == "none");
    CHECK(n.router.status()[0].msg == "WiFi has no internet");
    CHECK(n.via(1) == "tcp");
    CHECK(n.router.status()[2].msg == "gitlab is host-only, host doesn't offer gitlab");
    n.router.set_device_net(true);
    CHECK(n.via(0) == "device");
    CHECK(n.via(1) == "tcp");

    // The TCP host loses internet: sources that need it can't stay there.
    n.settings.set("sources", R"([{"sid":"w","src":"weather","via":"host"},{"sid":"s","src":"sys"}])");
    n.router.route();
    n.tcp.on_line(R"({"t":"host_state","net":false})", 2000);
    CHECK(n.via(0) == "none");
    CHECK(n.router.status()[0].msg == "host has no internet");
    CHECK(n.via(1) == "tcp");
}

TEST_CASE("DeviceFetcher: intervals, errors and retries")
{
    Net n;
    n.settings.set("sources", R"([{"sid":"btc","src":"http","every":10,)"
                              R"("p":{"url":"https://api.example/p","headers":{"X":"1"},)"
                              R"("pick":{"eur":"/bpi/EUR/rate_float","name":"/n","all":"/list","none":"/x"}}},)"
                              R"({"sid":"bad","src":"http","p":{"url":"ftp://x","pick":{"a":"/a"}}},)"
                              R"({"sid":"aqi","src":"aqi","every":60}])");
    n.settings.set("location.auto", "false");
    n.router.set_device_net(true);
    CHECK(n.router.status()[1].state == "error");
    CHECK(n.router.status()[1].msg == "params: url must be an absolute http(s) URL");

    n.answer.body = R"({"bpi":{"EUR":{"rate_float":61234.5}},"n":"Bitcoin","list":[1,2]})";
    n.device.run_due(); // aqi and btc now; aqi gets btc's answer
    REQUIRE(n.urls.size() == 2);
    CHECK(n.urls[1] == "https://api.example/p");
    CHECK(n.urls[0] == "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=51.2300&longitude=6.7800"
                       "&current=european_aqi,pm2_5,pm10");
    CHECK(n.store.number("btc.eur") == 61234.5);
    CHECK(n.store.text("btc.name") == "Bitcoin");
    CHECK(n.store.text("btc.all") == "[1,2]");
    CHECK_FALSE(n.store.get("btc.none"));
    auto st = n.router.status();
    CHECK(st[0].state == "ok");
    CHECK(st[0].every == 30); // clamped to the minimum
    CHECK(st[2].every == 900);

    // Next fetch after the interval; an error retries after 30 s at the latest.
    n.ms += 29000;
    CHECK(n.device.run_due() == 1000);
    n.ms += 1000;
    n.answer = {.status = 503, .body = "", .error = ""};
    CHECK(n.device.run_due() == 30000);
    CHECK(n.router.status()[0].msg == "HTTP 503");
    n.answer = {.status = 0, .body = "", .error = "DNS lookup failed"};
    n.device.retry();
    n.device.run_due();
    CHECK(n.router.status()[0].msg == "DNS lookup failed");
    n.answer = {.status = 200, .body = R"({"current":{"european_aqi":31.7,"pm2_5":7.04}})", .error = ""};
    n.ms += 900000;
    n.device.run_due();
    CHECK(n.store.number("aqi.index") == 31);
    CHECK(n.store.number("aqi.pm25") == doctest::Approx(7.0));
    CHECK(n.router.status()[0].msg == "no pick matched (eur, name, all, none)");
}

TEST_CASE("Location by IP address while location.auto is on")
{
    Net n;
    CHECK(n.settings.flag("location.auto"));
    REQUIRE(n.router.status().size() == 4);
    CHECK(n.router.status()[3].sid == "geo");

    // The device looks itself up; the result becomes the location, weather follows it.
    n.answer.body = R"({"ip":"1.2.3.4","city":"Hamburg","loc":"53.5507,9.9930","timezone":"Europe/Berlin"})";
    n.router.set_device_net(true);
    n.device.run_due();
    CHECK(n.router.status()[3].state == "ok");
    CHECK(n.urls[1] == "https://ipinfo.io/json"); // aqi, geo, weather: in sid order
    CHECK(n.store.number("geo.lat") == doctest::Approx(53.5507));
    auto found = core::auto_location(n.settings, n.store);
    CHECK(found == std::vector<std::pair<std::string, std::string>>{
                       {"location.lat", "53.5507"}, {"location.lon", "9.9930"}, {"location.name", "Hamburg"},
                       {"location.auto", "true"}});
    REQUIRE(n.settings.set_many(found) == std::nullopt);
    CHECK(n.settings.flag("location.auto"));
    CHECK(core::auto_location(n.settings, n.store).empty()); // stored already
    n.router.route();
    n.urls.clear();
    n.ms += 900000;
    n.device.run_due();
    CHECK(n.urls.front().find("latitude=53.5507&longitude=9.9930") != std::string::npos);

    // A time zone the firmware doesn't know is left as it is.
    n.store.put("geo.tz", std::string("Asia/Kathmandu"), core::Origin::Device, 0);
    CHECK(core::auto_location(n.settings, n.store).empty());
    n.store.put("geo.tz", std::string("Europe/London"), core::Origin::Device, 0);
    CHECK(core::auto_location(n.settings, n.store).front() ==
          std::pair<std::string, std::string>{"location.tz", "Europe/London"});

    // Set by hand: the lookup stops and its result no longer applies.
    REQUIRE(n.settings.set("location.lat", "48.1") == std::nullopt);
    CHECK_FALSE(n.settings.flag("location.auto"));
    CHECK(core::auto_location(n.settings, n.store).empty());
    n.router.route();
    CHECK(n.router.status().size() == 3);

    // Saving unchanged values (as an editor might) keeps it on.
    n.settings.set("location.auto", "true");
    CHECK(n.settings.set_many({{"location.lat", "48.1"}, {"wifi.ssid", "x"}}) == std::nullopt);
    CHECK(n.settings.flag("location.auto"));

    // A host offers geo when the WiFi has no internet.
    n.router.set_device_net(false);
    Net::connect(n.usb, R"(["time","geo"])");
    CHECK(n.via(3) == "usb");

    CHECK(fetch::check_params("geo", "") == std::nullopt);
    JsonDocument g;
    CHECK_FALSE(fetch::geo_values(R"({"loc":"x,y"})", g.to<JsonObject>()));
    CHECK_FALSE(fetch::geo_values(R"({"bogon":true})", g.to<JsonObject>()));
}

TEST_CASE("Settings saved before location.auto keep their location")
{
    core::MemoryStorage storage;
    storage.save("location.lat=48.1\n");
    CHECK_FALSE(core::Settings(storage).flag("location.auto"));
    core::MemoryStorage fresh;
    fresh.save("units.temp=F\n");
    CHECK(core::Settings(fresh).flag("location.auto"));
}

TEST_CASE("fetch: parameters, JSON Pointer, Open-Meteo")
{
    CHECK(fetch::check_params("weather", R"({"lat":51,"lon":7})") == std::nullopt);
    CHECK(fetch::check_params("weather", R"({"lat":91,"lon":7})") == "params: lat/lon out of range");
    CHECK(fetch::check_params("aqi", "") == "params: lat and lon are required");
    CHECK(fetch::check_params("time", "") == std::nullopt);
    CHECK(fetch::check_params("gitlab", "") == "gitlab is host-only");
    CHECK(fetch::check_params("http", R"({"url":"https://x/y","pick":{}})") ==
          "params: pick must name at least one value");
    CHECK(fetch::check_params("http", R"({"url":"https://x","pick":{"a..b":"/a"}})") ==
          R"(params: invalid key "a..b")");
    CHECK(fetch::check_params("http", R"({"url":"https://x","pick":{"a":"b"}})") ==
          R"(params: pointer for "a" must start with /)");
    CHECK(fetch::check_params("http", R"({"url":"http://x","pick":{"a.b":"/a"}})") == std::nullopt);

    JsonDocument d;
    deserializeJson(d, R"({"a":{"b/c":[10,{"~d":null}]},"e":0})");
    bool found = false;
    CHECK(fetch::pointer(d.as<JsonVariantConst>(), "/a/b~1c/0", found) == 10);
    CHECK(found);
    fetch::pointer(d.as<JsonVariantConst>(), "/a/b~1c/1/~0d", found);
    CHECK(found); // null, but there
    fetch::pointer(d.as<JsonVariantConst>(), "/a/x", found);
    CHECK_FALSE(found);
    fetch::pointer(d.as<JsonVariantConst>(), "/a/b~1c/2", found);
    CHECK_FALSE(found);
    CHECK(fetch::pointer(d.as<JsonVariantConst>(), "", found)["e"] == 0);

    JsonDocument w;
    REQUIRE(fetch::weather_message(kForecast, w));
    CHECK(w["now"]["temp"] == 8.4);
    CHECK(w["now"]["wind"] == 14.0);
    CHECK(w["rain_next_2h"].size() == 4);
    CHECK_FALSE(fetch::weather_message(R"({"error":true,"reason":"bad"})", w));
}

TEST_CASE("settings as JSON: values, schema, all-or-nothing set")
{
    core::MemoryStorage storage;
    core::Settings s(storage);
    s.set("wifi.pass", "x");
    JsonDocument d;
    core::settings_values(d.to<JsonObject>(), s, "wifi");
    CHECK(d.as<JsonObject>().size() == 2);
    CHECK(d["wifi.pass"] == "***");
    JsonDocument schema;
    core::settings_schema(schema.to<JsonArray>());
    CHECK(schema.size() == core::registry().size());
    CHECK(schema[1]["key"] == "wifi.ssid");
    CHECK(schema[2]["secret"] == true);

    JsonDocument set;
    deserializeJson(set, R"({"wifi.ssid":"Home","location.lat":48.1})");
    std::vector<std::string> keys;
    CHECK(core::apply_settings(s, set.as<JsonObjectConst>(), keys) == std::nullopt);
    CHECK(keys == std::vector<std::string>{"wifi.ssid", "location.lat"});
    CHECK(s.get("location.lat") == "48.1");
    deserializeJson(set, R"({"wifi.ssid":"Other","location.lat":100})");
    CHECK(core::apply_settings(s, set.as<JsonObjectConst>(), keys) == "location.lat must be from -90 to 90");
    CHECK(s.get("wifi.ssid") == "Home");
}

TEST_CASE("Improv Serial: state, info, scan, credentials")
{
    std::string out;
    std::vector<std::pair<std::string, std::string>> joined;
    std::string url;
    core::Improv improv({.firmware = "Blinky", .version = "0.2.0", .chip = "ESP32-S3", .name = "desk"},
                        {.send = [&](std::string_view b) { out += b; },
                         .connect = [&](const std::string &s, const std::string &p) { joined.emplace_back(s, p); },
                         .scan = [] { return std::vector<core::Improv::Network>{{"Home", -52, true}}; },
                         .url = [&] { return url; }});
    auto rpc = [](uint8_t cmd, std::string data) {
        std::string d(1, static_cast<char>(cmd));
        d += static_cast<char>(data.size());
        return core::Improv::packet(0x03, d + data);
    };

    // A packet split across reads, after a log line.
    const std::string state = rpc(0x02, "");
    improv.feed("I (12) boot: hi\n" + state.substr(0, 5));
    CHECK(out.empty());
    improv.feed(state.substr(5));
    CHECK(out == core::Improv::packet(0x01, "\x02")); // ready

    out.clear();
    improv.feed(rpc(0x03, ""));
    CHECK(out == core::Improv::rpc_result(0x03, {"Blinky", "0.2.0", "ESP32-S3", "desk"}));

    out.clear();
    improv.feed(rpc(0x04, ""));
    CHECK(out == core::Improv::rpc_result(0x04, {"Home", "-52", "YES"}) + core::Improv::rpc_result(0x04, {}));

    out.clear();
    improv.feed(rpc(0x01, std::string("\x04Home\x07secret!", 13)));
    REQUIRE(joined.size() == 1);
    CHECK(joined[0] == std::make_pair(std::string("Home"), std::string("secret!")));
    CHECK(out == core::Improv::packet(0x02, std::string(1, '\0')) + core::Improv::packet(0x01, "\x03"));
    out.clear();
    url = "http://192.168.1.23/";
    improv.connected(true);
    CHECK(out == core::Improv::packet(0x01, "\x04") + core::Improv::rpc_result(0x01, {url}));

    out.clear();
    improv.feed(state);
    CHECK(out == core::Improv::packet(0x01, "\x04") + core::Improv::rpc_result(0x02, {url}));

    // A bad checksum and an unknown command are errors.
    out.clear();
    std::string bad = state;
    bad[bad.size() - 2]++;
    improv.feed(bad);
    CHECK(out == core::Improv::packet(0x02, "\x01"));
    out.clear();
    improv.feed(rpc(0x09, ""));
    CHECK(out == core::Improv::packet(0x02, "\x02"));
}

TEST_CASE("captive DNS: every A question gets the device's address")
{
    // Query id 0x1234, recursion desired, one question: connectivitycheck.gstatic.com A IN.
    std::string q("\x12\x34\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00", 12);
    for (std::string label : {"connectivitycheck", "gstatic", "com"}) {
        q += static_cast<char>(label.size()) + label;
    }
    q += std::string("\x00\x00\x01\x00\x01", 5);
    const std::string r = core::captive_dns_reply(q, {192, 168, 4, 1});
    REQUIRE(r.size() == q.size() + 16);
    CHECK(r.substr(0, 2) == "\x12\x34");
    CHECK(static_cast<uint8_t>(r[2]) == 0x85);
    CHECK(r[7] == 1);
    CHECK(r.substr(12, q.size() - 12) == q.substr(12));
    CHECK(r.substr(r.size() - 4) == "\xC0\xA8\x04\x01");

    std::string aaaa = q;
    aaaa[aaaa.size() - 3] = 28;
    const std::string none = core::captive_dns_reply(aaaa, {192, 168, 4, 1});
    CHECK(none.size() == q.size());
    CHECK(none[7] == 0);

    CHECK(core::captive_dns_reply(r, {1, 2, 3, 4}).empty()); // a response
    CHECK(core::captive_dns_reply(q.substr(0, 20), {1, 2, 3, 4}).empty());
}

TEST_CASE("host:port and link descriptions")
{
    CHECK(core::host_port("192.168.1.5:7000", 7531) == std::make_pair(std::string("192.168.1.5"), uint16_t{7000}));
    CHECK(core::host_port("pve.lan", 7531) == std::make_pair(std::string("pve.lan"), uint16_t{7531}));
    CHECK_FALSE(core::host_port("pve.lan:", 7531));
    CHECK_FALSE(core::host_port("pve.lan:99999", 7531));
    CHECK_FALSE(core::host_port(":7531", 7531));

    Net n;
    CHECK(core::describe(n.usb).empty());
    n.usb.up(1000);
    CHECK(core::describe(n.usb) == "waiting for buddy-link");
    n.usb.on_line(R"({"t":"err","code":"auth","msg":"token missing"})", 1000);
    n.usb.on_line(R"({"t":"hello","v":1,"dev":"d","host":"buddy-link@pve"})", 1000);
    CHECK(core::describe(n.usb) == "buddy-link@pve");
    n.usb.on_line(R"({"t":"hello","v":1,"role":"setup"})", 9000);
    n.usb.on_line(R"({"t":"hello","v":1,"dev":"d","role":"setup"})", 9000);
    CHECK(core::describe(n.usb) == "setup page");
}
