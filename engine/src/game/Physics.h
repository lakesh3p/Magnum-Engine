#pragma once

#include "scene/Scene.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace magnum {

// A small rigid-body world for gameplay: gravity, collisions between boxes and spheres, triggers and ray casts.
//
// Colliders are boxes (an axis-aligned bound of the rotated box) or spheres. An actor takes part when it has a
// Collider; it moves by physics when it also has a Rigidbody that is not kinematic. A body pushes itself out of
// everything solid it touches and loses the velocity that pointed into it. Bodies do not push each other.
// Triggers report overlaps and block nothing.
class Physics {
public:
    struct Shape {
        bool sphere = false;
        glm::vec3 center{0.0f};
        glm::vec3 half{0.5f}; // boxes; for spheres every component is the radius
    };

    struct Hit {
        uint64_t actor = 0;
        float distance = 0.0f;
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
    };

    using Pair = std::pair<uint64_t, uint64_t>; // (first, second) as they started to touch

    // Forgets all state and takes the bodies from the scene.
    void Begin(Scene& scene);
    // Advances `dt` seconds in small fixed steps. Collision and trigger events of the call are collected below.
    void Step(Scene& scene, float dt);

    // Velocity and ground contact of a body (zero / false for actors without a rigidbody).
    glm::vec3 Velocity(uint64_t id) const;
    void SetVelocity(uint64_t id, const glm::vec3& velocity);
    bool Grounded(uint64_t id) const;
    void Forget(uint64_t id);

    // Started touching / overlapping during the last Step. A pair appears once, when it begins.
    const std::vector<Pair>& Hits() const { return m_hits; }
    const std::vector<Pair>& TriggerEnters() const { return m_enters; }
    const std::vector<Pair>& TriggerExits() const { return m_exits; }

    // The nearest solid collider the ray hits (triggers are ignored), skipping `ignore`.
    bool Raycast(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint64_t ignore,
                 Hit& out) const;

    // The collision volume of an actor, if it has a Collider.
    static bool ShapeOf(const Scene& scene, uint64_t id, Shape& out);

    float gravity = 9.81f;

private:
    struct Body {
        glm::vec3 velocity{0.0f};
        bool grounded = false;
    };

    void Substep(Scene& scene, float h);
    void DetectEvents(Scene& scene);

    std::unordered_map<uint64_t, Body> m_bodies;
    std::vector<Pair> m_hits, m_enters, m_exits;
    std::vector<Pair> m_touching, m_overlapping; // pairs that touched / overlapped at the end of the last step
};

} // namespace magnum
