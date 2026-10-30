#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace magnum {

// One game-defined component on an actor. Game components are registered at runtime by the game DLL,
// so they are stored as byte blocks (laid out as the game's struct) next to their type name.
struct GameInstance {
    std::string type;
    std::vector<uint8_t> data;  // valid while `resolved`
    std::string pendingJson;    // property values while the type is not registered (module missing / not loaded yet)
    bool resolved = false;
};

// Every game component of an actor. Several may be present, one per type.
struct GameComponentsComponent {
    std::vector<GameInstance> items;

    GameInstance* Find(const std::string& type) {
        for (GameInstance& i : items) {
            if (i.type == type) return &i;
        }
        return nullptr;
    }
    const GameInstance* Find(const std::string& type) const {
        for (const GameInstance& i : items) {
            if (i.type == type) return &i;
        }
        return nullptr;
    }
};

} // namespace magnum
