#pragma once

#include "render/Framebuffer.h"
#include "render/SceneRenderer.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace magnum::editor {

// A number that changes whenever a material, a shader or a texture changed (previews are redrawn then).
uint64_t AssetRevision(RenderResources& resources);

// Renders material preview spheres into small textures for the Content Browser and the material panel.
// A preview is redrawn when the material, a shader or a texture changed since it was last drawn.
class ThumbnailCache {
public:
    void BeginFrame() { m_budget = 6; } // limits redraws per frame so a big folder does not stall the editor
    void Clear() { m_entries.clear(); }

    // GL texture id of the preview, or 0 if the material cannot be drawn yet.
    uint32_t Material(SceneRenderer& renderer, RenderResources& resources, const std::string& materialPath, int size);
    // Same for a shader drawn with its default parameters (used by the material graph editor).
    uint32_t Shader(SceneRenderer& renderer, RenderResources& resources, const std::string& shaderPath, int size);

private:
    struct Entry {
        std::unique_ptr<Framebuffer> fb;
        uint64_t revision = ~0ull;
    };
    std::map<std::string, Entry> m_entries; // key: path + "#" + size
    int m_budget = 6;
};

} // namespace magnum::editor
