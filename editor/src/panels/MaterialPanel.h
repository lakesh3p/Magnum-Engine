#pragma once

#include "EditorContext.h"
#include "panels/MaterialInspector.h"
#include "render/Framebuffer.h"

#include <string>

namespace magnum::editor {

// Focused material view: a large preview sphere, the shader it uses, compile errors and all parameters.
// Shows the material picked in the Content Browser, otherwise the selected actor's material.
class MaterialPanel {
public:
    void Draw(EditorContext& ctx);

private:
    MaterialInspector m_inspector;

    // The big preview sphere: drawn into its own target so it can be turned with the mouse.
    Framebuffer m_previewFb;
    float m_yaw = 0.0f, m_pitch = 0.0f; // degrees
    uint64_t m_drawnRevision = ~0ull;
    float m_drawnYaw = 0.0f, m_drawnPitch = 0.0f;
    std::string m_drawnPath;
    int m_drawnSize = 0;
};

} // namespace magnum::editor
