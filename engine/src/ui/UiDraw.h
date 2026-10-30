#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace magnum {

// What the UI asks the renderer to draw this frame: triangles in view pixels (origin top left) grouped into commands
// that share a texture and a clip rectangle. Building it needs no graphics context.
struct UiVertex {
    glm::vec2 pos{0.0f};    // pixels
    glm::vec2 uv{0.0f};
    glm::vec4 color{1.0f};
    // Rounded rectangle the pixel belongs to: half size, corner radius, border width (all pixels).
    glm::vec4 shape{0.0f};
    glm::vec4 border{0.0f}; // border color
    glm::vec2 local{0.0f};  // pixel position relative to the rectangle's center, before any rotation
    glm::vec2 misc{0.0f};   // x: mode (see UiVertexMode), y: edge softness in pixels (shadows)
};

enum UiVertexMode : int {
    kUiShape = 0,   // a rounded rectangle (rendered from `shape`), multiplied by the texture
    kUiText = 1,    // the texture is a one-channel glyph atlas; its value is the coverage
};

struct UiDrawCommand {
    int image = -1;           // index into UiDrawList::images, -1 = none
    int atlas = -1;           // font atlas id, -1 = none
    bool pixelated = false;   // nearest-neighbour sampling for the image
    bool clipped = false;
    int clip[4] = {0, 0, 0, 0}; // x, y, width, height in pixels from the top left
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

struct UiDrawList {
    std::vector<UiVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<UiDrawCommand> commands;
    std::vector<std::string> images; // project-relative paths
    int viewWidth = 0, viewHeight = 0;

    void Clear() {
        vertices.clear();
        indices.clear();
        commands.clear();
        images.clear();
    }
    bool Empty() const { return commands.empty(); }
};

} // namespace magnum
