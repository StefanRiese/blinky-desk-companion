#include "app_core/settings_nvs.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace core {
namespace {
constexpr const char *kTag = "settings";
constexpr const char *kNamespace = "cfg";
constexpr const char *kKey = "settings";
} // namespace

bool NvsStorage::init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "NVS format changed, erasing");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err == ESP_OK;
}

std::optional<std::string> NvsStorage::load()
{
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) {
        return std::nullopt; // nothing saved yet
    }
    size_t size = 0;
    std::optional<std::string> out;
    if (nvs_get_blob(h, kKey, nullptr, &size) == ESP_OK) {
        std::string block(size, '\0');
        if (nvs_get_blob(h, kKey, block.data(), &size) == ESP_OK) {
            out = std::move(block);
        }
    }
    nvs_close(h);
    return out;
}

bool NvsStorage::save(std::string_view block)
{
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_set_blob(h, kKey, block.data(), block.size()) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (!ok) {
        ESP_LOGE(kTag, "saving settings failed");
    }
    return ok;
}

} // namespace core
