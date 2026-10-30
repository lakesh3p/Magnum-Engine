#include "scene/SpriteSize.h"

#include "core/Paths.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

namespace magnum {

namespace {

struct CachedSize {
    std::filesystem::file_time_type stamp{};
    int width = 0, height = 0;
};

} // namespace

bool ImageSize(const std::string& relPath, int& width, int& height) {
    if (relPath.empty()) return false;
    static std::map<std::string, CachedSize> cache;
    const std::filesystem::path path = ResolveAsset(ProjectRoot(), relPath);
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec) return false;

    CachedSize& entry = cache[relPath];
    if (entry.stamp != stamp || entry.width == 0) {
        entry.stamp = stamp;
        entry.width = entry.height = 0;
        std::ifstream in(path, std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        int channels = 0;
        if (!bytes.empty()) stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &entry.width, &entry.height, &channels);
    }
    width = entry.width;
    height = entry.height;
    return width > 0 && height > 0;
}

glm::vec2 SpriteWorldSize(const SpriteComponent& s) {
    if (s.width > 0.0f && s.height > 0.0f) return {s.width, s.height};
    int w = 0, h = 0;
    if (!ImageSize(s.texture, w, h)) return {s.width > 0.0f ? s.width : 1.0f, s.height > 0.0f ? s.height : 1.0f};
    const float ppu = std::max(s.pixelsPerUnit, 0.001f);
    const float cellW = static_cast<float>(w) / static_cast<float>(std::max(s.columns, 1)) / ppu;
    const float cellH = static_cast<float>(h) / static_cast<float>(std::max(s.rows, 1)) / ppu;
    // One side given: keep the cell's proportions.
    if (s.width > 0.0f) return {s.width, s.width * cellH / cellW};
    if (s.height > 0.0f) return {s.height * cellW / cellH, s.height};
    return {cellW, cellH};
}

glm::vec4 SpriteUvRect(const SpriteComponent& s) {
    const int columns = std::max(s.columns, 1), rows = std::max(s.rows, 1);
    const int cells = columns * rows;
    const int frame = std::clamp(s.frame, 0, cells - 1);
    const int column = frame % columns, row = frame / columns; // row 0 is the top of the image
    const float du = 1.0f / static_cast<float>(columns), dv = 1.0f / static_cast<float>(rows);
    float u0 = static_cast<float>(column) * du;
    float v0 = 1.0f - static_cast<float>(row + 1) * dv; // images are uploaded bottom row first
    float w = du, h = dv;
    if (s.flipX) { u0 += w; w = -w; }
    if (s.flipY) { v0 += h; h = -h; }
    return {u0, v0, w, h};
}

void AdvanceSpriteAnimation(SpriteComponent& s, float dt) {
    if (!s.animate) return;
    const int cells = std::max(s.columns, 1) * std::max(s.rows, 1);
    const int first = std::clamp(s.firstFrame, 0, cells - 1);
    const int count = std::clamp(s.frameCount > 0 ? s.frameCount : cells - first, 1, cells - first);
    s.animationTime += dt;
    const int step = static_cast<int>(std::floor(s.animationTime * std::max(s.fps, 0.0f)));
    if (s.loop) {
        s.frame = first + step % count;
    } else {
        s.frame = first + std::min(step, count - 1);
        if (step >= count - 1) s.animate = false; // a one-shot animation rests on its last frame
    }
}

} // namespace magnum
