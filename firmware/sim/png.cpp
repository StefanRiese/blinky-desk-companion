#include "png.h"

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace png {
namespace {

const uint8_t kSignature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

void put32(std::vector<uint8_t> &out, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8) {
        out.push_back(static_cast<uint8_t>(v >> s));
    }
}

uint32_t get32(const uint8_t *p)
{
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
}

void chunk(std::ofstream &f, const char *type, const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> c;
    put32(c, static_cast<uint32_t>(data.size()));
    c.insert(c.end(), type, type + 4);
    c.insert(c.end(), data.begin(), data.end());
    put32(c, static_cast<uint32_t>(crc32(0, c.data() + 4, static_cast<uInt>(c.size() - 4))));
    f.write(reinterpret_cast<const char *>(c.data()), static_cast<std::streamsize>(c.size()));
}

} // namespace

bool write_rgb(const std::string &path, int width, int height, const std::vector<uint8_t> &rgb)
{
    // Scanlines: filter byte 0 + RGB.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (width * 3 + 1));
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + static_cast<long>(y) * width * 3,
                   rgb.begin() + static_cast<long>(y + 1) * width * 3);
    }
    uLongf size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> z(size);
    if (compress2(z.data(), &size, raw.data(), static_cast<uLong>(raw.size()), 9) != Z_OK) {
        return false;
    }
    z.resize(size);

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(reinterpret_cast<const char *>(kSignature), sizeof kSignature);
    std::vector<uint8_t> ihdr;
    put32(ihdr, static_cast<uint32_t>(width));
    put32(ihdr, static_cast<uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8-bit, RGB, deflate, no filter, no interlace
    chunk(f, "IHDR", ihdr);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    return static_cast<bool>(f);
}

bool read_rgb(const std::string &path, int &width, int &height, std::vector<uint8_t> &rgb)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < 8 || !std::equal(std::begin(kSignature), std::end(kSignature), data.begin())) {
        return false;
    }
    std::vector<uint8_t> z;
    for (size_t pos = 8; pos + 12 <= data.size();) {
        const uint32_t len = get32(&data[pos]);
        const std::string type(reinterpret_cast<const char *>(&data[pos + 4]), 4);
        const uint8_t *body = &data[pos + 8];
        if (pos + 12 + len > data.size()) {
            return false;
        }
        if (type == "IHDR") {
            width = static_cast<int>(get32(body));
            height = static_cast<int>(get32(body + 4));
            if (body[8] != 8 || body[9] != 2 || body[12] != 0) {
                return false; // only 8-bit RGB, not interlaced
            }
        } else if (type == "IDAT") {
            z.insert(z.end(), body, body + len);
        }
        pos += 12 + len;
    }
    std::vector<uint8_t> raw(static_cast<size_t>(height) * (width * 3 + 1));
    uLongf size = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &size, z.data(), static_cast<uLong>(z.size())) != Z_OK || size != raw.size()) {
        return false;
    }
    rgb.resize(static_cast<size_t>(width) * height * 3);
    for (int y = 0; y < height; y++) {
        const uint8_t *line = &raw[static_cast<size_t>(y) * (width * 3 + 1)];
        if (line[0] != 0) {
            return false; // filtered scanlines are not supported
        }
        std::copy(line + 1, line + 1 + width * 3, rgb.begin() + static_cast<long>(y) * width * 3);
    }
    return true;
}

} // namespace png
