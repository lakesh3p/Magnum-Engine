#pragma once

#include "scene/Components.h"

#include <imgui.h>

namespace magnum::editor {

// Vector icons drawn straight into an ImDrawList: crisp at any size, tintable, no image files.
enum class Icon {
    Play, Stop, Save, Undo, Redo,
    Move, Rotate, Scale, World, Local, Snap,
    Folder, Cube, Sphere, Plane, Light, Actor,
    Search, Add, Trash, Reload, Grid,
    Material, Graph, Texture, Code, Scene,
    ChevronRight, ChevronDown, Up, Dot,
    Select, Pause, Cloud, Person, Camera, Lock, Maximize, ChevronLeft,
    Cylinder, Cone, Capsule, Hemisphere, Torus, Arch, Pyramid, Wedge, Prism, Diamond, Stairs, Disc, Tube, Ring,
    Shader, Script, Collider, Rigidbody,
    Count
};

// The icon that stands for a mesh primitive (Outliner rows, Place Actors).
Icon PrimitiveIcon(Primitive primitive);

// `size` is the icon's nominal width/height in pixels; the drawing is centered on `center`.
void DrawIcon(ImDrawList* drawList, Icon icon, ImVec2 center, float size, ImU32 color);

// A square button with an icon. `active` shows it as toggled on. `tint` (0 = default) colors the icon.
bool IconButton(const char* id, Icon icon, const char* tooltip, bool active = false, float size = 30.0f, ImU32 tint = 0);

// An icon followed by text on the current line (the icon is as tall as the text).
void IconText(Icon icon, const char* text, ImU32 iconColor = 0);

// Draws every icon large with its name; a debugging aid for tuning the shapes (console: `icons`).
void ShowIconSheet(bool* open);

} // namespace magnum::editor
