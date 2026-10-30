#pragma once

#include "scene/GameComponents.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace magnum {

// A full copy of one actor's components, used to restore deleted actors (undo) and to duplicate.
// Components are kept by name, so a snapshot stays usable after the game module is reloaded.
struct ActorSnapshot {
    uint64_t id = 0;
    size_t index = 0; // position in the scene order
    std::vector<std::pair<std::string, std::shared_ptr<void>>> components; // built-in components, copied as they are
    // Game components as property JSON by field name: a hot reload between taking and restoring the snapshot
    // may have changed the struct layout, so raw bytes would not be safe.
    std::vector<std::pair<std::string, std::string>> gameComponents;
    std::vector<GameInstance> unresolvedGame; // game components whose type is not registered right now
};

ActorSnapshot TakeSnapshot(const Scene& scene, uint64_t id);
// Recreates the actor with the same id at the same position in the order.
void RestoreSnapshot(Scene& scene, const ActorSnapshot& snapshot);

} // namespace magnum
