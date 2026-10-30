#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace magnum {

// Writes bottom-up RGBA8 pixels (OpenGL order) as a top-down opaque PNG.
bool SavePng(const std::string& path, int width, int height, std::vector<uint8_t> rgbaBottomUp);

// Reads the currently bound read framebuffer (default: the back buffer) and saves it.
bool SaveBackBufferPng(const std::string& path, int width, int height);

} // namespace magnum
