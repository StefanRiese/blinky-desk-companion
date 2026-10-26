// Minimal PNG writer (8-bit RGB, uncompressed deflate blocks): enough for screenshots,
// with no library dependency.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace png {

bool write_rgb(const std::string &path, int width, int height, const std::vector<uint8_t> &rgb);

} // namespace png
