#pragma once

#include "EditorCamera.h"
#include "scene/Scene.h"

namespace magnum::editor {

// Nearest actor whose oriented bounding box the ray hits, or 0.
uint64_t PickActor(const Scene& scene, const Ray& ray);

} // namespace magnum::editor
