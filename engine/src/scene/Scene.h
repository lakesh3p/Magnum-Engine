#pragma once

#include "scene/Components.h"

#include <entt/entt.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace magnum {

// Actors are entities that always carry Id, Name and Transform; everything else is an optional component.
class Scene {
public:
    static constexpr size_t kEnd = static_cast<size_t>(-1);

    // id 0 = generate one. index = position in Order() (kEnd = append).
    uint64_t CreateActor(const std::string& name, uint64_t id = 0, size_t index = kEnd);
    void DestroyActor(uint64_t id);
    void Clear();

    entt::entity Find(uint64_t id) const;
    bool Exists(uint64_t id) const { return Find(id) != entt::null; }

    entt::registry& Registry() { return m_registry; }
    const entt::registry& Registry() const { return m_registry; }
    const std::vector<uint64_t>& Order() const { return m_order; }

    template <class T>
    T* TryGet(uint64_t id) {
        const entt::entity e = Find(id);
        return e == entt::null ? nullptr : m_registry.try_get<T>(e);
    }
    template <class T>
    const T* TryGet(uint64_t id) const {
        const entt::entity e = Find(id);
        return e == entt::null ? nullptr : m_registry.try_get<T>(e);
    }

    // `base`, or `base_1`, `base_2`... if the name is already taken.
    std::string UniqueName(const std::string& base) const;

    // Half-size of the actor's local bounding box (used for picking and the selection box).
    glm::vec3 LocalHalfExtents(uint64_t id) const;

private:
    entt::registry m_registry;
    std::unordered_map<uint64_t, entt::entity> m_lookup;
    std::vector<uint64_t> m_order;
};

} // namespace magnum
