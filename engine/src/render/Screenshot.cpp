#include "render/Screenshot.h"

#include "core/Log.h"

#include <glad/gl.h>

#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace magnum {

bool SavePng(const std::string& path, int width, int height, std::vector<uint8_t> rgbaBottomUp) {
    for (size_t i = 3; i < rgbaBottomUp.size(); i += 4) {
        rgbaBottomUp[i] = 255;
    }

    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    stbi_flip_vertically_on_write(1);
    const bool ok = stbi_write_png(path.c_str(), width, height, 4, rgbaBottomUp.data(), width * 4) != 0;
    if (ok) {
        Log::Info("Screenshot saved: {} ({}x{})", path, width, height);
    } else {
        Log::Error("Failed to write screenshot: {}", path);
    }
    return ok;
}

bool SaveBackBufferPng(const std::string& path, int width, int height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return SavePng(path, width, height, std::move(pixels));
}

} // namespace magnum
