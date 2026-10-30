#include "Picking.h"

#include <algorithm>
#include <limits>

namespace magnum::editor {

uint64_t PickActor(const Scene& scene, const Ray& ray) {
    uint64_t best = 0;
    float bestT = std::numeric_limits<float>::max();

    for (const uint64_t id : scene.Order()) {
        const TransformComponent* t = scene.TryGet<TransformComponent>(id);
        if (!t) continue;

        // Slab test in the actor's local space. The ray is not renormalized, so the
        // hit parameter is the same in world space.
        const glm::mat4 inv = glm::inverse(t->Matrix());
        const glm::vec3 o = glm::vec3(inv * glm::vec4(ray.origin, 1.0f));
        const glm::vec3 d = glm::vec3(inv * glm::vec4(ray.dir, 0.0f));
        const glm::vec3 h = scene.LocalHalfExtents(id);

        float tMin = 0.0f;
        float tMax = std::numeric_limits<float>::max();
        bool hit = true;
        for (int axis = 0; axis < 3 && hit; ++axis) {
            if (std::abs(d[axis]) < 1e-8f) {
                hit = std::abs(o[axis]) <= h[axis];
            } else {
                float t0 = (-h[axis] - o[axis]) / d[axis];
                float t1 = (h[axis] - o[axis]) / d[axis];
                if (t0 > t1) std::swap(t0, t1);
                tMin = std::max(tMin, t0);
                tMax = std::min(tMax, t1);
                hit = tMin <= tMax;
            }
        }
        if (hit && tMin < bestT) {
            bestT = tMin;
            best = id;
        }
    }
    return best;
}

} // namespace magnum::editor
