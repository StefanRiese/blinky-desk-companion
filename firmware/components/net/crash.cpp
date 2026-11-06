// The last crash, from the core dump partition (written by ESP-IDF's panic handler): a short
// summary for the boot log and the web page (task, exception, program counter, backtrace),
// and the raw dump for esp-coredump (pip install esp-coredump; no ESP-IDF needed). The
// addresses turn into source lines with the app's .elf from the same release:
//   xtensa-esp32s3-elf-addr2line -pfiaC -e blinky-app-<board>.elf <pc> <backtrace…>
#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <cstdio>
#include <string>

#include "esp_core_dump.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "net_internal.h"

namespace net {
namespace {

constexpr const char *kTag = "crash";

// cause_name names the common Xtensa exception causes (EXCCAUSE).
const char *cause_name(uint32_t c)
{
    switch (c) {
    case 0:
        return "illegal instruction";
    case 2:
        return "instruction fetch error";
    case 3:
        return "load/store error";
    case 6:
        return "division by zero";
    case 9:
        return "unaligned load/store";
    case 20:
        return "instruction fetch prohibited";
    case 28:
        return "load prohibited (bad pointer)";
    case 29:
        return "store prohibited (bad pointer)";
    default:
        return "other (abort, assert or watchdog)";
    }
}

std::string hex(uint32_t v)
{
    char buf[12];
    std::snprintf(buf, sizeof buf, "0x%08" PRIx32, v);
    return buf;
}

} // namespace

bool crash_summary(JsonObject out)
{
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK) {
        return false; // no dump (or an empty or broken partition)
    }
    out["size"] = size;
    esp_core_dump_summary_t s{};
    if (esp_core_dump_get_summary(&s) != ESP_OK) {
        out["error"] = "the dump can't be read here; download it";
        return true;
    }
    out["task"] = s.exc_task;
    out["cause"] = cause_name(s.ex_info.exc_cause);
    out["exccause"] = s.ex_info.exc_cause;
    out["vaddr"] = hex(s.ex_info.exc_vaddr);
    out["pc"] = hex(s.exc_pc);
    JsonArray bt = out["backtrace"].to<JsonArray>();
    for (uint32_t i = 0; i < s.exc_bt_info.depth && i < 16; i++) {
        bt.add(hex(s.exc_bt_info.bt[i]));
    }
    out["backtrace_corrupted"] = s.exc_bt_info.corrupted;
    out["elf_sha256"] = std::string(reinterpret_cast<const char *>(s.app_elf_sha256),
                                    strnlen(reinterpret_cast<const char *>(s.app_elf_sha256), sizeof s.app_elf_sha256));
    return true;
}

void log_crash()
{
    JsonDocument doc;
    if (!crash_summary(doc.to<JsonObject>())) {
        return;
    }
    std::string bt;
    for (JsonVariantConst a : doc["backtrace"].as<JsonArrayConst>()) {
        bt += " " + std::string(a.as<const char *>());
    }
    ESP_LOGW(kTag, "last crash: task %s, %s, pc %s, backtrace%s (details at /api/crash)", doc["task"] | "?",
             doc["cause"] | "?", doc["pc"] | "?", bt.c_str());
}

bool crash_read(const std::function<bool(const char *data, size_t n)> &out)
{
    size_t addr = 0, size = 0;
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
    if (p == nullptr || esp_core_dump_image_get(&addr, &size) != ESP_OK || addr < p->address) {
        return false;
    }
    std::string buf(2048, '\0'); // on the heap: the web server's stack is small
    for (size_t off = 0; off < size; off += buf.size()) {
        const size_t n = std::min(buf.size(), size - off);
        if (esp_partition_read(p, addr - p->address + off, buf.data(), n) != ESP_OK || !out(buf.data(), n)) {
            return false;
        }
    }
    return true;
}

bool crash_erase()
{
    return esp_core_dump_image_erase() == ESP_OK;
}

} // namespace net
