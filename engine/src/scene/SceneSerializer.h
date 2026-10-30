#pragma once

#include "scene/Scene.h"

#include <filesystem>
#include <string>

namespace magnum {

// Editor-only data stored next to the scene so reopening it looks the same.
struct SceneEditorState {
    glm::vec3 cameraPosition{0.0f};
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;
    bool hasCamera = false;
};

// .scene files are indented JSON, written from the component descriptions in Reflection.h:
//   { "magnumScene": 1, "editor": {...}, "actors": [ { "id": "...", "components": { "Transform": { "Position": [..] } } } ] }
// Load builds into a temporary scene and only replaces `scene` when everything parsed, so a broken
// file never leaves a half-loaded level. Unknown components/properties are skipped with a warning.
// The same format as text in memory (used to snapshot the scene when play starts).
bool SerializeScene(const Scene& scene, const SceneEditorState& editor, std::string& out, std::string& error);
bool DeserializeScene(Scene& scene, SceneEditorState& editor, const std::string& text, std::string& error);

// One component's properties as a JSON object (by property name) and back. Used to carry game component
// values across a hot reload, where the struct layout may have changed. Returns false on invalid JSON.
struct ComponentInfo;
std::string SerializeProperties(const ComponentInfo& info, const void* component);
bool ApplyProperties(const ComponentInfo& info, void* component, const std::string& jsonText, const std::string& where);

bool SaveScene(const Scene& scene, const SceneEditorState& editor, const std::filesystem::path& file,
               std::string& error);
bool LoadScene(Scene& scene, SceneEditorState& editor, const std::filesystem::path& file, std::string& error);

} // namespace magnum
