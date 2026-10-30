#pragma once

#include "EditorContext.h"
#include "commands/SpawnKinds.h"
#include "scene/Reflection.h"

#include <string>

namespace magnum::editor {



// All of these go through ctx.commands and select the affected actor where it makes sense.
// `at` is where the actor goes (on the ground there); without it the actor appears in front of the camera.
uint64_t SpawnActor(EditorContext& ctx, SpawnKind kind, const glm::vec3* at = nullptr);
// A mesh actor that shows a model file (any format the importer reads), named after the file.
uint64_t SpawnModel(EditorContext& ctx, const std::string& relPath, const glm::vec3* at = nullptr);
void DeleteActor(EditorContext& ctx, uint64_t id);
uint64_t DuplicateActor(EditorContext& ctx, uint64_t id);

// Sets one property through the undo stack (discrete edits, resets, console `set`).
void SetPropertyCommand(EditorContext& ctx, uint64_t id, const ComponentInfo& info, size_t propIndex,
                        const PropertyValue& value);
// Records an edit that was already applied live while dragging; `before` is the value from before the drag.
void RecordPropertyEdit(EditorContext& ctx, uint64_t id, const ComponentInfo& info, size_t propIndex,
                        const PropertyValue& before, const PropertyValue& after);

// Same for a whole transform (gizmo drags change position, rotation and scale together).
void RecordTransformEdit(EditorContext& ctx, uint64_t id, const TransformComponent& before,
                         const TransformComponent& after);

void AddComponentCommand(EditorContext& ctx, uint64_t id, const ComponentInfo& info);
void RemoveComponentCommand(EditorContext& ctx, uint64_t id, const ComponentInfo& info);

} // namespace magnum::editor
