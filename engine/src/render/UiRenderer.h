#pragma once

#include "render/Texture.h"
#include "shader/ShaderProgram.h"
#include "ui/UiDraw.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace magnum {

// Draws a UiDrawList into the bound framebuffer: rounded rectangles (with borders and soft shadows), images and
// glyphs, batched by texture and scissor rectangle. Uses straight alpha blending, so it can be drawn over anything.
class UiRenderer {
public:
    void Init();
    void Shutdown();
    void Draw(const UiDrawList& list, TextureLibrary& textures, int viewWidth, int viewHeight);

private:
    struct AtlasTexture {
        uint32_t id = 0;
        uint32_t version = 0;
        int width = 0, height = 0;
    };

    uint32_t FontTexture(int atlas);

    std::unique_ptr<ShaderProgram> m_program;
    uint32_t m_vao = 0, m_vbo = 0, m_ibo = 0;
    uint32_t m_samplers[2] = {0, 0}; // smooth, pixelated
    std::unordered_map<int, AtlasTexture> m_atlases;
};

} // namespace magnum
