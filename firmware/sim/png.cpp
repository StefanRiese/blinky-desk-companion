#include "png.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace png {
namespace {

uint32_t crc32(const uint8_t *data, size_t n, uint32_t crc = 0)
{
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

void put32(std::vector<uint8_t> &out, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8) {
        out.push_back(static_cast<uint8_t>(v >> s));
    }
}

void chunk(std::ofstream &f, const char *type, const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> c;
    put32(c, static_cast<uint32_t>(data.size()));
    c.insert(c.end(), type, type + 4);
    c.insert(c.end(), data.begin(), data.end());
    put32(c, crc32(c.data() + 4, c.size() - 4));
    f.write(reinterpret_cast<const char *>(c.data()), static_cast<std::streamsize>(c.size()));
}

} // namespace

bool write_rgb(const std::string &path, int width, int height, const std::vector<uint8_t> &rgb)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    static const uint8_t sig[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    f.write(reinterpret_cast<const char *>(sig), sizeof sig);

    std::vector<uint8_t> ihdr;
    put32(ihdr, static_cast<uint32_t>(width));
    put32(ihdr, static_cast<uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8-bit, RGB, deflate, no filter, no interlace
    chunk(f, "IHDR", ihdr);

    // Raw scanlines (filter byte 0 + RGB), stored in uncompressed deflate blocks (zlib).
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (width * 3 + 1));
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + static_cast<long>(y) * width * 3,
                   rgb.begin() + static_cast<long>(y + 1) * width * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0; // Adler-32
    for (size_t pos = 0; pos < raw.size();) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n));
        z.push_back(static_cast<uint8_t>(~n >> 8));
        for (size_t i = 0; i < n; i++) {
            const uint8_t v = raw[pos + i];
            z.push_back(v);
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
        pos += n;
    }
    put32(z, (b << 16) | a);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    return static_cast<bool>(f);
}

} // namespace png
