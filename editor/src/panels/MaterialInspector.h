#pragma once

#include "EditorContext.h"
#include "material/MaterialData.h"

#include <string>
#include <vector>

namespace magnum::editor {

// Parameter rows for one material, generated from the parameters its shader declares:
// sliders for ranged floats, drags for vectors, color pickers for "color", texture slots for "texture".
// Edits change the material in memory right away (the preview updates live), are written to the
// material file when the drag ends, and are recorded in the undo stack. Reset (x) removes the override.
class MaterialInspector {
public:
    void Draw(EditorContext& ctx, const std::string& materialPath);

private:
    void DrawTextureSlot(EditorContext& ctx, MaterialAsset& material, const ShaderParam& param);
    // Replaces the material's data as one finished edit (immediate commit + undo entry).
    void Commit(EditorContext& ctx, MaterialAsset& material, const MaterialData& after);

    bool m_editing = false;     // a drag is in progress
    MaterialData m_before;      // material data when that drag started
    std::vector<std::string> m_textureChoices;
};

} // namespace magnum::editor
