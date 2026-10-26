// Small PNG writer and reader for screenshots (8-bit RGB, zlib-compressed). The reader
// handles the files this writer makes (no filters, no interlacing), which is all the
// screenshot tests need.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace png {

bool write_rgb(const std::string &path, int width, int height, const std::vector<uint8_t> &rgb);

// read_rgb loads a PNG written by write_rgb. Returns false if missing or in another format.
bool read_rgb(const std::string &path, int &width, int &height, std::vector<uint8_t> &rgb);

} // namespace png
