// OtaReceiver takes a firmware update from a host, as "ota" messages over a host link
// (protocol/PROTOCOL.md): begin (size, SHA-256, version), numbered Base64 blocks, end. It
// checks the order, the size and the SHA-256 and writes through hooks (esp_ota_* on the
// device, a buffer in tests). After end the firmware restarts into the new app; if that
// doesn't start, the bootloader rolls back. Mirrors the emulator (host/internal/sim/ota.go).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <ArduinoJson.h>

namespace core {

// Sha256 hashes data in pieces (small and portable; the update checks with it).
class Sha256 {
public:
    Sha256();
    void update(const uint8_t *data, size_t len);
    std::array<uint8_t, 32> finish();
    static std::string hex(const std::array<uint8_t, 32> &digest);

private:
    void block(const uint8_t *p);
    std::array<uint32_t, 8> h_;
    std::array<uint8_t, 64> buf_{};
    size_t used_ = 0;
    uint64_t total_ = 0;
};

// base64_decode decodes standard Base64 (with or without padding); nullopt if invalid.
std::optional<std::string> base64_decode(std::string_view in);

class OtaReceiver {
public:
    static constexpr size_t kMaxBlock = 1024; // bytes per data message (fits one line)

    struct Hooks {
        // begin prepares to receive size bytes; returns an error message or nullopt.
        std::function<std::optional<std::string>(size_t size)> begin;
        std::function<std::optional<std::string>(const uint8_t *data, size_t len)> write;
        // finish checks the written image and makes it the one to boot.
        std::function<std::optional<std::string>()> finish;
        std::function<void()> abort;
    };

    explicit OtaReceiver(Hooks hooks) : hooks_(std::move(hooks)) {}

    // handle takes one "ota" message and returns the reply line (ack or err).
    std::string handle(JsonVariantConst m);
    // cancel stops an update in progress (the link went away).
    void cancel();

    bool active() const { return active_; }
    // finished: the new firmware is ready; the caller restarts the device.
    bool finished() const { return finished_; }
    int percent() const { return size_ == 0 ? 0 : static_cast<int>(received_ * 100 / size_); }
    const std::string &version() const { return version_; }

private:
    Hooks hooks_;
    bool active_ = false, finished_ = false;
    size_t size_ = 0, received_ = 0;
    int next_seq_ = 0;
    std::string sha_, version_;
    Sha256 hash_;
};

} // namespace core
