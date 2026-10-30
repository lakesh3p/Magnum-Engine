#pragma once

#include "EditorContext.h"

namespace magnum::editor {

// The UI inspector: the canvases of the running game (or of the scene's preview) as a tree of nodes, and the properties
// of the selected node. Changes act at once (a quick way to try sizes, colors and layouts) but are not written to the
// document: copy what you like into the .lua file. The selected node is outlined in the viewport.
class UiPanel {
public:
    void Draw(EditorContext& ctx);

private:
    void DrawNodeTree(EditorContext& ctx, UiSystem& ui, UiId id);
    void DrawProperties(EditorContext& ctx, UiSystem& ui, UiNode& node);
};

} // namespace magnum::editor
