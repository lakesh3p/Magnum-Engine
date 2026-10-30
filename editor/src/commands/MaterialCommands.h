#pragma once

#include "EditorContext.h"
#include "material/MaterialData.h"

namespace magnum::editor {

// Records that a material's data changed from `before` to `after` (already applied in memory) and writes the
// file. Undo/redo restore the data and rewrite the file. These edits do not mark the scene as modified.
void RecordMaterialEdit(EditorContext& ctx, const std::string& materialPath, const MaterialData& before,
                        const MaterialData& after);

} // namespace magnum::editor
