#pragma once

#include "EditorContext.h"

namespace magnum::editor {

// The list of things that can be added to the level: dense rows, a filter, collapsible groups. A click adds the actor
// in front of the camera; dragging a row into the viewport drops it on the ground under the mouse (payload "MAGNUM_SPAWN").
class PlaceActorsPanel {
public:
    void Draw(EditorContext& ctx);

private:
    char m_filter[64] = {};
    bool m_open[7] = {true, true, true, true, true, true, true};
};

} // namespace magnum::editor
