#include "scene/ActorSnapshot.h"

#include "scene/SceneSerializer.h"

#include <algorithm>

namespace magnum {

ActorSnapshot TakeSnapshot(const Scene& scene, uint64_t id) {
    ActorSnapshot snap;
    snap.id = id;
    const auto& order = scene.Order();
    snap.index = static_cast<size_t>(std::find(order.begin(), order.end(), id) - order.begin());

    const entt::entity e = scene.Find(id);
    if (e == entt::null) return snap;
    for (const ComponentInfo* info : AllComponentInfos()) {
        if (!info->has(scene.Registry(), e)) continue;
        const std::shared_ptr<void> copy = info->clone(scene.Registry(), e);
        if (info->game) snap.gameComponents.emplace_back(info->name, SerializeProperties(*info, copy.get()));
        else snap.components.emplace_back(info->name, copy);
    }
    if (const auto* game = scene.Registry().try_get<GameComponentsComponent>(e)) {
        for (const GameInstance& item : game->items) {
            if (!item.resolved) snap.unresolvedGame.push_back(item);
        }
    }
    return snap;
}

void RestoreSnapshot(Scene& scene, const ActorSnapshot& snap) {
    scene.CreateActor("", snap.id, snap.index);
    const entt::entity e = scene.Find(snap.id);
    for (const auto& [name, data] : snap.components) {
        if (const ComponentInfo* info = FindComponentInfo(name)) {
            info->restore(scene.Registry(), e, data.get());
        }
    }
    for (const auto& [name, json] : snap.gameComponents) {
        if (const ComponentInfo* info = FindComponentInfo(name)) {
            info->add(scene.Registry(), e);
            ApplyProperties(*info, info->get(scene.Registry(), e), json, "restored actor");
        } else { // the type is gone (module reloaded without it): keep the values for when it returns
            GameInstance item;
            item.type = name;
            item.pendingJson = json;
            scene.Registry().get_or_emplace<GameComponentsComponent>(e).items.push_back(std::move(item));
        }
    }
    if (!snap.unresolvedGame.empty()) {
        auto& game = scene.Registry().get_or_emplace<GameComponentsComponent>(e);
        for (const GameInstance& item : snap.unresolvedGame) {
            if (!game.Find(item.type)) game.items.push_back(item);
        }
    }
}

} // namespace magnum
