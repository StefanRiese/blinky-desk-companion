// Improv Serial (https://www.improv-wifi.com/serial/): lets ESP Web Tools on the setup page
// send WiFi credentials right after flashing, over the same USB port as the host link.
// Packets: "IMPROV", version 1, type, length, data, checksum (sum of all bytes before it),
// then a newline. They never start with '{', so buddyd and the NDJSON reader ignore them.
// Thread-safe: feed() runs in the USB reader task, connected() in the WiFi event task.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace core {

class Improv {
public:
    enum class State : uint8_t { Ready = 0x02, Provisioning = 0x03, Provisioned = 0x04 };
    enum class Error : uint8_t { None = 0x00, InvalidRpc = 0x01, UnknownRpc = 0x02, UnableToConnect = 0x03 };
    enum Command : uint8_t { kWifiSettings = 0x01, kCurrentState = 0x02, kDeviceInfo = 0x03, kScan = 0x04 };

    struct Network {
        std::string ssid;
        int rssi = 0;
        bool secured = true;
    };

    struct Hooks {
        std::function<void(std::string_view bytes)> send;
        // connect saves the credentials and connects; report the outcome with connected().
        std::function<void(const std::string &ssid, const std::string &pass)> connect;
        std::function<std::vector<Network>()> scan;
        // url is where the device can be reached once it is on the WiFi ("" = not connected).
        std::function<std::string()> url;
    };

    struct Info {
        std::string firmware, version, chip, name;
    };

    Improv(Info info, Hooks hooks) : info_(std::move(info)), hooks_(std::move(hooks)) {}

    void feed(std::string_view bytes);
    // connected reports the outcome of a connect: ok with the device's URL, or not.
    void connected(bool ok);

    // packet builds one packet (with checksum and newline).
    static std::string packet(uint8_t type, std::string_view data);
    static std::string rpc_result(uint8_t command, const std::vector<std::string> &strings);

private:
    void handle(uint8_t type, std::string_view data);
    void send_state(State s);
    void send_error(Error e);

    Info info_;
    Hooks hooks_;
    std::recursive_mutex mu_;
    std::string buf_;
    bool provisioning_ = false;
};

} // namespace core
