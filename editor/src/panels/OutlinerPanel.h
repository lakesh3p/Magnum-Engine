#pragma once

#include "EditorContext.h"

#include <string>

namespace magnum::editor {

// Flat list of the scene's actors with search, rename, duplicate and delete.
class OutlinerPanel {
public:
    void Draw(EditorContext& ctx);

private:
    void DrawRenamePopup(EditorContext& ctx);

    char m_filter[128] = {};
    uint64_t m_renameId = 0;
    char m_renameBuffer[128] = {};
    bool m_openRename = false;
};

} // namespace magnum::editor
