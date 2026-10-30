#include "game/Physics.h"

#include <algorithm>
#include <cmath>

namespace magnum {

namespace {

constexpr float kStep = 1.0f / 120.0f; // fixed step: a body can cross a thin floor only above ~12 m/s per cm of thickness
constexpr int kMaxSubsteps = 12;
constexpr float kSkin = 0.02f;         // contact tolerance for hit events

struct Push {
    glm::vec3 normal{0.0f}; // direction the first shape must move
    float depth = 0.0f;
};

bool BoxBox(const Physics::Shape& a, const Physics::Shape& b, Push& out) {
    const glm::vec3 d = a.center - b.center;
    const glm::vec3 overlap = a.half + b.half - glm::abs(d);
    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) return false;
    int axis = 0;
    if (overlap.y < overlap[axis]) axis = 1;
    if (overlap.z < overlap[axis]) axis = 2;
    out.normal = glm::vec3(0.0f);
    out.normal[axis] = d[axis] >= 0.0f ? 1.0f : -1.0f;
    out.depth = overlap[axis];
    return true;
}

// `sphere` against the box `box`; the normal points from the box towards the sphere.
bool SphereBox(const Physics::Shape& sphere, const Physics::Shape& box, Push& out) {
    const glm::vec3 lo = box.center - box.half, hi = box.center + box.half;
    const glm::vec3 closest = glm::clamp(sphere.center, lo, hi);
    glm::vec3 d = sphere.center - closest;
    const float radius = sphere.half.x;
    const float distance = glm::length(d);
    if (distance > 1e-6f) {
        if (distance >= radius) return false;
        out.normal = d / distance;
        out.depth = radius - distance;
        return true;
    }
    // The center is inside the box: leave through the nearest face.
    const glm::vec3 toLo = sphere.center - lo, toHi = hi - sphere.center;
    float best = 1e30f;
    out.normal = glm::vec3(0, 1, 0);
    for (int axis = 0; axis < 3; ++axis) {
        if (toLo[axis] < best) { best = toLo[axis]; out.normal = glm::vec3(0.0f); out.normal[axis] = -1.0f; }
        if (toHi[axis] < best) { best = toHi[axis]; out.normal = glm::vec3(0.0f); out.normal[axis] = 1.0f; }
    }
    out.depth = best + radius;
    return true;
}

bool SphereSphere(const Physics::Shape& a, const Physics::Shape& b, Push& out) {
    const glm::vec3 d = a.center - b.center;
    const float distance = glm::length(d);
    const float sum = a.half.x + b.half.x;
    if (distance >= sum) return false;
    out.normal = distance > 1e-6f ? d / distance : glm::vec3(0, 1, 0);
    out.depth = sum - distance;
    return true;
}

// How far `a` must move to leave `b` (false if they do not overlap). `grow` inflates both a little.
bool Collide(const Physics::Shape& a, const Physics::Shape& b, Push& out) {
    if (!a.sphere && !b.sphere) return BoxBox(a, b, out);
    if (a.sphere && b.sphere) return SphereSphere(a, b, out);
    if (a.sphere) return SphereBox(a, b, out);
    if (!SphereBox(b, a, out)) return false;
    out.normal = -out.normal; // the normal pointed from a to b: a leaves the other way
    return true;
}

bool RayBox(const glm::vec3& o, const glm::vec3& d, const Physics::Shape& box, float maxDistance, float& t, glm::vec3& normal) {
    const glm::vec3 lo = box.center - box.half, hi = box.center + box.half;
    float tMin = 0.0f, tMax = maxDistance;
    glm::vec3 enter(0.0f);
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(d[axis]) < 1e-8f) {
            if (o[axis] < lo[axis] || o[axis] > hi[axis]) return false;
            continue;
        }
        float t0 = (lo[axis] - o[axis]) / d[axis], t1 = (hi[axis] - o[axis]) / d[axis];
        float sign = -1.0f;
        if (t0 > t1) { std::swap(t0, t1); sign = 1.0f; }
        if (t0 > tMin) { tMin = t0; enter = glm::vec3(0.0f); enter[axis] = sign; }
        tMax = std::min(tMax, t1);
        if (tMin > tMax) return false;
    }
    t = tMin;
    normal = glm::dot(enter, enter) > 0.0f ? enter : glm::vec3(0, 1, 0);
    return true;
}

bool RaySphere(const glm::vec3& o, const glm::vec3& d, const Physics::Shape& sphere, float maxDistance, float& t, glm::vec3& normal) {
    const glm::vec3 m = o - sphere.center;
    const float r = sphere.half.x;
    const float b = glm::dot(m, d);
    const float c = glm::dot(m, m) - r * r;
    if (c > 0.0f && b > 0.0f) return false;
    const float disc = b * b - c;
    if (disc < 0.0f) return false;
    t = std::max(-b - std::sqrt(disc), 0.0f);
    if (t > maxDistance) return false;
    normal = glm::normalize(o + d * t - sphere.center);
    return true;
}

Physics::Pair Ordered(uint64_t a, uint64_t b) {
    return a < b ? Physics::Pair{a, b} : Physics::Pair{b, a};
}

bool Contains(const std::vector<Physics::Pair>& list, const Physics::Pair& p) {
    return std::find(list.begin(), list.end(), p) != list.end();
}

} // namespace

bool Physics::ShapeOf(const Scene& scene, uint64_t id, Shape& out) {
    const ColliderComponent* collider = scene.TryGet<ColliderComponent>(id);
    const TransformComponent* t = scene.TryGet<TransformComponent>(id);
    if (!collider || !t) return false;

    glm::vec3 local = scene.LocalHalfExtents(id);
    const MeshComponent* mesh = scene.TryGet<MeshComponent>(id);
    const glm::vec3 scale = glm::abs(t->scale);
    glm::vec3 half = local * scale * glm::abs(collider->size);

    // Rotation: the box becomes the bound of the rotated box.
    const glm::mat3 r(t->RotationMatrix());
    half = glm::vec3(std::abs(r[0][0]) * half.x + std::abs(r[1][0]) * half.y + std::abs(r[2][0]) * half.z,
                     std::abs(r[0][1]) * half.x + std::abs(r[1][1]) * half.y + std::abs(r[2][1]) * half.z,
                     std::abs(r[0][2]) * half.x + std::abs(r[1][2]) * half.y + std::abs(r[2][2]) * half.z);

    out.center = t->position;
    if (collider->shape == ColliderShape::Sphere) {
        out.sphere = true;
        out.half = glm::vec3(std::max({half.x, half.y, half.z}));
    } else {
        out.sphere = false;
        out.half = half;
        // A plane is infinitely thin: give it a body below the surface so nothing falls through.
        if (mesh && mesh->primitive == Primitive::Plane) {
            const float thickness = 0.1f;
            out.center.y -= thickness - out.half.y;
            out.half.y = thickness;
        }
    }
    return true;
}

void Physics::Begin(Scene& scene) {
    m_bodies.clear();
    m_hits.clear();
    m_enters.clear();
    m_exits.clear();
    m_touching.clear();
    m_overlapping.clear();
    for (const uint64_t id : scene.Order()) {
        if (scene.TryGet<RigidbodyComponent>(id)) m_bodies[id] = Body{};
    }
    // Pairs that already overlap when play starts do not count as events.
    DetectEvents(scene);
    m_hits.clear();
    m_enters.clear();
    m_exits.clear();
}

glm::vec3 Physics::Velocity(uint64_t id) const {
    const auto it = m_bodies.find(id);
    return it == m_bodies.end() ? glm::vec3(0.0f) : it->second.velocity;
}

void Physics::SetVelocity(uint64_t id, const glm::vec3& velocity) {
    m_bodies[id].velocity = velocity; // a body is created on first use (a script may add a rigidbody late)
}

bool Physics::Grounded(uint64_t id) const {
    const auto it = m_bodies.find(id);
    return it != m_bodies.end() && it->second.grounded;
}

void Physics::Forget(uint64_t id) {
    m_bodies.erase(id);
}

void Physics::Step(Scene& scene, float dt) {
    m_hits.clear();
    m_enters.clear();
    m_exits.clear();
    if (dt <= 0.0f) return;
    dt = std::min(dt, 0.1f); // a long frame (a breakpoint, a hitch) must not fling bodies around
    const int steps = std::clamp(static_cast<int>(std::ceil(dt / kStep)), 1, kMaxSubsteps);
    for (int i = 0; i < steps; ++i) Substep(scene, dt / static_cast<float>(steps));
    DetectEvents(scene);
}

void Physics::Substep(Scene& scene, float h) {
    // Colliders that can block a body.
    std::vector<std::pair<uint64_t, Shape>> solids;
    for (const uint64_t id : scene.Order()) {
        const ColliderComponent* c = scene.TryGet<ColliderComponent>(id);
        if (!c || c->trigger) continue;
        Shape shape;
        if (ShapeOf(scene, id, shape)) solids.emplace_back(id, shape);
    }

    for (const uint64_t id : scene.Order()) {
        const RigidbodyComponent* rb = scene.TryGet<RigidbodyComponent>(id);
        const ColliderComponent* collider = scene.TryGet<ColliderComponent>(id);
        TransformComponent* t = scene.TryGet<TransformComponent>(id);
        if (!rb || !t) continue;
        Body& body = m_bodies[id];
        if (rb->kinematic) {
            body.velocity = glm::vec3(0.0f);
            continue;
        }

        if (rb->lockZ) body.velocity.z = 0.0f;
        if (rb->lockZ) body.velocity.z = 0.0f;
        if (rb->useGravity) body.velocity.y -= gravity * rb->gravityScale * h;
        if (rb->drag > 0.0f) body.velocity *= std::max(0.0f, 1.0f - rb->drag * h);
        t->position += body.velocity * h;
        body.grounded = false;
        if (!collider || collider->trigger) continue;

        for (int pass = 0; pass < 3; ++pass) { // a corner can need more than one push
            bool moved = false;
            Shape me;
            if (!ShapeOf(scene, id, me)) break;
            for (const auto& [otherId, other] : solids) {
                if (otherId == id) continue;
                Push push;
                if (!Collide(me, other, push)) continue;
                if (rb->lockZ && std::abs(push.normal.z) > 0.5f) continue; // a 2D body is never pushed along Z
                t->position += push.normal * push.depth;
                me.center += push.normal * push.depth;
                const float into = glm::dot(body.velocity, push.normal);
                if (into < 0.0f) body.velocity -= push.normal * into; // stop sliding into the surface
                if (push.normal.y > 0.5f) body.grounded = true;
                moved = true;
            }
            if (!moved) break;
        }
    }
}

void Physics::DetectEvents(Scene& scene) {
    struct Item {
        uint64_t id;
        Shape shape;
        bool trigger;
        bool moves; // has a rigidbody: only pairs with at least one such actor produce events
    };
    std::vector<Item> items;
    for (const uint64_t id : scene.Order()) {
        const ColliderComponent* c = scene.TryGet<ColliderComponent>(id);
        Shape shape;
        if (c && ShapeOf(scene, id, shape)) items.push_back({id, shape, c->trigger, scene.TryGet<RigidbodyComponent>(id) != nullptr});
    }

    std::vector<Pair> touching, overlapping;
    for (size_t i = 0; i < items.size(); ++i) {
        for (size_t j = i + 1; j < items.size(); ++j) {
            const Item& a = items[i];
            const Item& b = items[j];
            if (!a.moves && !b.moves) continue;
            // Triggers also react to static colliders; trigger against trigger is ignored.
            if (a.trigger && b.trigger) continue;
            Shape ga = a.shape, gb = b.shape;
            if (a.trigger || b.trigger) {
                Push push;
                if (Collide(ga, gb, push)) overlapping.push_back(Ordered(a.id, b.id));
            } else {
                ga.half += glm::vec3(kSkin);
                Push push;
                if (Collide(ga, gb, push)) touching.push_back(Ordered(a.id, b.id));
            }
        }
    }

    for (const Pair& p : touching) {
        if (!Contains(m_touching, p)) m_hits.push_back(p);
    }
    for (const Pair& p : overlapping) {
        if (!Contains(m_overlapping, p)) m_enters.push_back(p);
    }
    for (const Pair& p : m_overlapping) {
        if (!Contains(overlapping, p)) m_exits.push_back(p);
    }
    m_touching = std::move(touching);
    m_overlapping = std::move(overlapping);
}

bool Physics::Raycast(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint64_t ignore,
                      Hit& out) const {
    const float length = glm::length(direction);
    if (length < 1e-8f) return false;
    const glm::vec3 d = direction / length;
    bool found = false;
    float best = maxDistance;
    for (const uint64_t id : scene.Order()) {
        if (id == ignore) continue;
        const ColliderComponent* c = scene.TryGet<ColliderComponent>(id);
        Shape shape;
        if (!c || c->trigger || !ShapeOf(scene, id, shape)) continue;
        float t = 0.0f;
        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        const bool hit = shape.sphere ? RaySphere(origin, d, shape, best, t, normal) : RayBox(origin, d, shape, best, t, normal);
        if (hit && t <= best) {
            best = t;
            found = true;
            out.actor = id;
            out.distance = t;
            out.point = origin + d * t;
            out.normal = normal;
        }
    }
    return found;
}

} // namespace magnum
