#pragma once

#include "EditorContext.h"
#include "panels/MaterialInspector.h"
#include "scene/Reflection.h"

namespace magnum::editor {

// Property inspector for the selected actor, built from the component descriptions in Reflection.h:
// collapsible components and categories, search, reset-to-default and undo for every edit.
class DetailsPanel {
public:
    void Draw(EditorContext& ctx);

private:
    // Draws the value widget and routes edits through the command stack.
    void EditProperty(EditorContext& ctx, uint64_t id, const ComponentInfo& info, size_t propIndex, void* component);
    void DrawComponent(EditorContext& ctx, uint64_t id, const ComponentInfo& info, void* component);

    MaterialInspector m_materialInspector;
    char m_filter[128] = {};
    bool m_editing = false;      // a drag/typing session is in progress
    PropertyValue m_before;      // value at the start of that session
    const ComponentInfo* m_removeRequest = nullptr;
};

} // namespace magnum::editor
