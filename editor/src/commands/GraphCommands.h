#pragma once

#include "EditorContext.h"

#include <filesystem>
#include <string>

namespace magnum::editor {

// Writes a text file by writing a temporary next to it and renaming, so a reader never sees half a file.
bool WriteTextFile(const std::filesystem::path& file, const std::string& text);

// Records that the text of a graph shader changed from `before` to `after` (the file already holds `after`).
// Undo/redo write the file again; the graph panel notices the change and reloads its graph, and the shader
// hot-reloads like after any other edit. These edits do not mark the scene as modified.
void RecordGraphEdit(EditorContext& ctx, const std::string& shaderPath, const std::string& before, const std::string& after);

} // namespace magnum::editor
