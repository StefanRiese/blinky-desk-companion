// Settings: the device's configuration, defined in one registry (key, type, default, range,
// choices, secret) that every editor uses (web config, setup page, config messages).
// Keys and defaults match the emulator (host/internal/sim/nvs.go). The values are stored as
// one text block through a Storage (NVS on the device, memory in the simulator and tests).
#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace core {

enum class Type { Bool, Int, Float, String, Enum, Json };

struct Setting {
    std::string_view key;
    Type type;
    std::string_view def; // default, as text
    double min = 0, max = 0; // for Int and Float (min == max: no range)
    std::span<const std::string_view> choices = {}; // for Enum
    bool secret = false;   // never shown again; masked as "***"
};

// registry lists all settings.
std::span<const Setting> registry();
const Setting *find_setting(std::string_view key);

// Storage keeps the settings block (load returns nullopt when nothing was saved yet).
class Storage {
public:
    virtual ~Storage() = default;
    virtual std::optional<std::string> load() = 0;
    virtual bool save(std::string_view block) = 0;
};

class MemoryStorage : public Storage {
public:
    std::optional<std::string> load() override { return block_; }
    bool save(std::string_view block) override
    {
        block_ = std::string(block);
        return true;
    }

private:
    std::optional<std::string> block_;
};

class Settings {
public:
    static constexpr std::string_view kMasked = "***";

    // Settings loads what storage holds; unknown keys in it are kept but not used.
    explicit Settings(Storage &storage);

    // get returns the value as text (the default if not set).
    std::string get(std::string_view key) const;
    double number(std::string_view key) const;
    bool flag(std::string_view key) const;

    // shown returns the value for editors: secrets that are set come back as "***".
    std::string shown(std::string_view key) const;

    // set validates and stores one value; returns an error message, or nullopt on success.
    // Setting a secret to "***" keeps the stored secret (editors send back what they got).
    std::optional<std::string> set(std::string_view key, std::string_view value);

    // reset removes a value, so its default applies again.
    void reset(std::string_view key);

private:
    std::optional<std::string> validate(const Setting &s, std::string_view value) const;
    void save_locked();

    Storage &storage_;
    mutable std::mutex mu_;
    std::map<std::string, std::string, std::less<>> values_;
};

// Encoding of the stored block: one "key=value" per line, with \\ and \n escaped.
std::string encode_block(const std::map<std::string, std::string, std::less<>> &values);
std::map<std::string, std::string, std::less<>> decode_block(std::string_view block);

} // namespace core
