#include "scene/Scene.h"

#include "model/ModelImport.h"
#include "scene/SpriteSize.h"

#include <algorithm>
#include <random>
#include <unordered_set>

namespace magnum {

namespace {

uint64_t RandomId() {
    static std::mt19937_64 rng{std::random_device{}()};
    uint64_t id = 0;
    while (id == 0) id = rng();
    return id;
}

} // namespace

uint64_t Scene::CreateActor(const std::string& name, uint64_t id, size_t index) {
    if (id == 0) {
        do { id = RandomId(); } while (m_lookup.contains(id));
    }
    const entt::entity e = m_registry.create();
    m_registry.emplace<IdComponent>(e, IdComponent{id});
    m_registry.emplace<NameComponent>(e, NameComponent{name});
    m_registry.emplace<TransformComponent>(e);
    m_lookup[id] = e;

    if (index >= m_order.size()) m_order.push_back(id);
    else m_order.insert(m_order.begin() + static_cast<std::ptrdiff_t>(index), id);
    return id;
}

void Scene::DestroyActor(uint64_t id) {
    const auto it = m_lookup.find(id);
    if (it == m_lookup.end()) return;
    m_registry.destroy(it->second);
    m_lookup.erase(it);
    m_order.erase(std::remove(m_order.begin(), m_order.end(), id), m_order.end());
}

void Scene::Clear() {
    m_registry.clear();
    m_lookup.clear();
    m_order.clear();
}

entt::entity Scene::Find(uint64_t id) const {
    const auto it = m_lookup.find(id);
    return it == m_lookup.end() ? entt::entity{entt::null} : it->second;
}

std::string Scene::UniqueName(const std::string& base) const {
    std::unordered_set<std::string> used;
    for (const uint64_t id : m_order) {
        used.insert(m_registry.get<NameComponent>(m_lookup.at(id)).name);
    }
    if (!used.contains(base)) return base;
    for (int i = 1;; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (!used.contains(candidate)) return candidate;
    }
}

glm::vec3 Scene::LocalHalfExtents(uint64_t id) const {
    if (const SpriteComponent* sprite = TryGet<SpriteComponent>(id)) {
        const glm::vec2 size = SpriteWorldSize(*sprite);
        return {size.x * 0.5f, size.y * 0.5f, 0.5f}; // deep enough that 2D bodies always overlap along Z first
    }
    if (const MeshComponent* mesh = TryGet<MeshComponent>(id)) {
        if (std::shared_ptr<const ImportedModel> model = LoadModel(mesh->model)) {
            return glm::max(model->Size() * 0.5f, glm::vec3(0.005f)); // a flat model still gets a body with thickness
        }
        return PrimitiveHalfExtents(mesh->primitive);
    }
    if (TryGet<DecalComponent>(id)) return glm::vec3(0.5f); // the projection box itself
    return glm::vec3(0.25f); // empties and lights: a small marker box
}

} // namespace magnum
