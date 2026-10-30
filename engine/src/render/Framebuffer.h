#pragma once

#include <cstdint>
#include <vector>

namespace magnum {

// Offscreen RGBA8 color + depth/stencil target. The editor viewport renders into one.
class Framebuffer {
public:
    Framebuffer() = default;
    ~Framebuffer();
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    // Recreates the attachments if the size changed. Sizes below 1 are clamped.
    void Resize(int width, int height);
    void Release() { Destroy(); m_width = m_height = 0; } // frees the GL objects now
    void Bind() const;
    static void BindDefault();

    uint32_t ColorTexture() const { return m_color; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }

    // Bottom-up RGBA8 pixels, as stored by OpenGL.
    std::vector<uint8_t> ReadPixels() const;

private:
    void Destroy();

    uint32_t m_fbo = 0;
    uint32_t m_color = 0;
    uint32_t m_depth = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace magnum
