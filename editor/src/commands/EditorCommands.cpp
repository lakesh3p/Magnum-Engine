#include "commands/EditorCommands.h"

#include "scene/ActorSnapshot.h"
#include "scene/SceneSerializer.h"

#include <cmath>
#include <filesystem>
#include <functional>

namespace magnum::editor {

namespace {

class AddActorCommand final : public Command {
public:
    using Builder = std::function<void(Scene&, uint64_t)>;

    AddActorCommand(Scene& scene, std::string baseName, Builder builder)
        : m_scene(scene), m_baseName(std::move(baseName)), m_builder(std::move(builder)) {}

    void Do() override {
        if (!m_created) {
            m_id = m_scene.CreateActor(m_scene.UniqueName(m_baseName));
            if (m_builder) m_builder(m_scene, m_id);
            m_snapshot = TakeSnapshot(m_scene, m_id);
            m_created = true;
        } else {
            RestoreSnapshot(m_scene, m_snapshot);
        }
    }
    void Undo() override { m_scene.DestroyActor(m_id); }
    std::string Name() const override { return "Add " + m_baseName; }
    uint64_t Id() const { return m_id; }

private:
    Scene& m_scene;
    std::string m_baseName;
    Builder m_builder;
    ActorSnapshot m_snapshot;
    uint64_t m_id = 0;
    bool m_created = false;
};

class DeleteActorCommand final : public Command {
public:
    DeleteActorCommand(Scene& scene, uint64_t id) : m_scene(scene), m_id(id) {
        if (const NameComponent* n = scene.TryGet<NameComponent>(id)) m_name = n->name;
    }
    void Do() override {
        m_snapshot = TakeSnapshot(m_scene, m_id);
        m_scene.DestroyActor(m_id);
    }
    void Undo() override { RestoreSnapshot(m_scene, m_snapshot); }
    std::string Name() const override { return "Delete " + m_name; }

private:
    Scene& m_scene;
    uint64_t m_id;
    std::string m_name;
    ActorSnapshot m_snapshot;
};

class SetPropertyCmd final : public Command {
public:
    // Component and property are kept by name and looked up on every Do/Undo: the game module can be
    // reloaded between an edit and its undo, which replaces the ComponentInfos.
    SetPropertyCmd(Scene& scene, uint64_t id, const ComponentInfo& info, size_t prop, PropertyValue before,
                   PropertyValue after)
        : m_scene(scene), m_id(id), m_component(info.name), m_property(info.props[prop].name),
          m_before(std::move(before)), m_after(std::move(after)) {}

    void Do() override { Apply(m_after); }
    void Undo() override { Apply(m_before); }
    std::string Name() const override { return "Edit " + m_property; }

private:
    void Apply(const PropertyValue& v) {
        const ComponentInfo* info = FindComponentInfo(m_component);
        const entt::entity e = m_scene.Find(m_id);
        if (!info || e == entt::null) return;
        void* component = info->get(m_scene.Registry(), e);
        if (!component) return;
        for (const PropertyInfo& p : info->props) {
            if (m_property == p.name) SetProperty(p, component, v);
        }
    }
    Scene& m_scene;
    uint64_t m_id;
    std::string m_component, m_property;
    PropertyValue m_before, m_after;
};

class TransformCmd final : public Command {
public:
    TransformCmd(Scene& scene, uint64_t id, const TransformComponent& before, const TransformComponent& after)
        : m_scene(scene), m_id(id), m_before(before), m_after(after) {}

    void Do() override { Apply(m_after); }
    void Undo() override { Apply(m_before); }
    std::string Name() const override { return "Transform"; }

private:
    void Apply(const TransformComponent& t) {
        if (TransformComponent* c = m_scene.TryGet<TransformComponent>(m_id)) *c = t;
    }
    Scene& m_scene;
    uint64_t m_id;
    TransformComponent m_before, m_after;
};

class ComponentCmd final : public Command {
public:
    ComponentCmd(Scene& scene, uint64_t id, const ComponentInfo& info, bool add)
        : m_scene(scene), m_id(id), m_component(info.name), m_add(add) {}

    void Do() override { m_add ? Add() : Remove(); }
    void Undo() override { m_add ? Remove() : Add(); }
    std::string Name() const override { return (m_add ? "Add " : "Remove ") + m_component; }

private:
    void Add() {
        const ComponentInfo* info = FindComponentInfo(m_component);
        const entt::entity e = m_scene.Find(m_id);
        if (!info || e == entt::null) return;
        info->add(m_scene.Registry(), e);
        // Values go back by property name, so this also works after a game module reload changed the layout.
        if (!m_saved.empty()) ApplyProperties(*info, info->get(m_scene.Registry(), e), m_saved, m_component);
    }
    void Remove() {
        const ComponentInfo* info = FindComponentInfo(m_component);
        const entt::entity e = m_scene.Find(m_id);
        if (!info || e == entt::null) return;
        if (void* data = info->get(m_scene.Registry(), e)) m_saved = SerializeProperties(*info, data);
        info->remove(m_scene.Registry(), e);
    }
    Scene& m_scene;
    uint64_t m_id;
    std::string m_component;
    bool m_add;
    std::string m_saved; // property JSON of the removed component
};

// Where new actors appear: on the ground in front of the camera, or straight ahead.
glm::vec3 SpawnPoint(const EditorContext& ctx) {
    if (ctx.view2D) return glm::vec3(ctx.view2DCenter.x, ctx.view2DCenter.y, 0.0f); // the middle of the 2D view
    const glm::vec3 fwd = ctx.camera.Forward();
    const glm::vec3 pos = ctx.camera.position;
    if (fwd.y < -0.05f) {
        const float t = std::clamp(-pos.y / fwd.y, 2.0f, 40.0f);
        glm::vec3 p = pos + fwd * t;
        p.y = 0.0f;
        return p;
    }
    return pos + fwd * 8.0f;
}

} // namespace

// `model` is a model file for the mesh of the new actor ("" = the kind's primitive); the actor is then named after it.
static uint64_t SpawnActorWith(EditorContext& ctx, SpawnKind kind, const glm::vec3* at, const std::string& model) {
    const SpawnInfo info = GetSpawnInfo(kind);
    const glm::vec3 point = at ? *at : SpawnPoint(ctx);

    auto builder = [info, point, model, projectRoot = ctx.projectRoot](Scene& scene, uint64_t id) {
        entt::registry& reg = scene.Registry();
        const entt::entity e = scene.Find(id);
        TransformComponent& t = reg.get<TransformComponent>(e);
        t.position = point;
        t.position.y = point.y + info.lift;
        t.position.z = point.z + info.depth;
        t.scale = glm::vec3(info.scaleX, info.scaleY, info.scaleZ);
        if (info.isMesh) {
            MeshComponent mesh;
            mesh.primitive = info.primitive;
            mesh.model = model;
            if (info.primitive == Primitive::Plane && info.scaleX == 1.0f) t.scale = glm::vec3(4.0f);
            reg.emplace<MeshComponent>(e, mesh);
        } else if (info.kind == SpawnKind::DirectionalLight) {
            t.rotation = glm::vec3(-50.0f, 30.0f, 0.0f);
            reg.emplace<LightComponent>(e);
        }
        if (info.features & kFeatureSprite) reg.emplace<SpriteComponent>(e);
        if (info.features & kFeatureCanvas) {
            CanvasComponent canvas;
            for (const char* candidate : {"Content/UI/hud.lua", "Content/UI/menu.lua"}) { // a first document, when the project has one
                if (std::filesystem::exists(projectRoot / candidate)) {
                    canvas.document = candidate;
                    break;
                }
            }
            reg.emplace<CanvasComponent>(e, canvas);
        }
        if (info.features & kFeatureDecal) {
            reg.emplace<DecalComponent>(e);
            t.rotation.x = 90.0f; // projects downwards onto the ground it is dropped on
        }
        if (info.features & kFeatureCamera) {
            CameraComponent camera;
            camera.orthographic = (info.features & kFeatureOrtho) != 0;
            reg.emplace<CameraComponent>(e, camera);
        }
        if (info.features & kFeatureCollider) {
            ColliderComponent collider;
            collider.trigger = (info.features & kFeatureTrigger) != 0;
            if (info.features & kFeatureSphereCollider) collider.shape = ColliderShape::Sphere;
            reg.emplace<ColliderComponent>(e, collider);
        }
        if (info.features & kFeatureRigidbody) {
            RigidbodyComponent body;
            body.lockZ = (info.features & kFeatureLockZ) != 0;
            reg.emplace<RigidbodyComponent>(e, body);
        }
        if (info.script[0]) reg.emplace<ScriptComponent>(e, ScriptComponent{info.script});
    };

    auto command = std::make_unique<AddActorCommand>(ctx.scene, model.empty() ? info.name : std::filesystem::path(model).stem().string(), builder);
    AddActorCommand* raw = command.get();
    ctx.commands.Execute(std::move(command));
    ctx.selected = raw->Id();
    return raw->Id();
}

uint64_t SpawnActor(EditorContext& ctx, SpawnKind kind, const glm::vec3* at) {
    return SpawnActorWith(ctx, kind, at, "");
}

uint64_t SpawnModel(EditorContext& ctx, const std::string& relPath, const glm::vec3* at) {
    return SpawnActorWith(ctx, SpawnKind::Cube, at, relPath);
}

void DeleteActor(EditorContext& ctx, uint64_t id) {
    if (!ctx.scene.Exists(id)) return;
    ctx.commands.Execute(std::make_unique<DeleteActorCommand>(ctx.scene, id));
    if (ctx.selected == id) ctx.selected = 0;
}

uint64_t DuplicateActor(EditorContext& ctx, uint64_t id) {
    if (!ctx.scene.Exists(id)) return 0;
    const ActorSnapshot source = TakeSnapshot(ctx.scene, id);
    const std::string baseName = ctx.scene.TryGet<NameComponent>(id)->name;

    auto builder = [source](Scene& scene, uint64_t newId) {
        const entt::entity e = scene.Find(newId);
        for (const auto& [name, data] : source.components) {
            if (name == "Name") continue; // keep the unique name
            if (const ComponentInfo* info = FindComponentInfo(name)) info->restore(scene.Registry(), e, data.get());
        }
        for (const auto& [name, json] : source.gameComponents) {
            if (const ComponentInfo* info = FindComponentInfo(name)) {
                info->add(scene.Registry(), e);
                ApplyProperties(*info, info->get(scene.Registry(), e), json, "duplicate");
            }
        }
        if (!source.unresolvedGame.empty()) {
            auto& game = scene.Registry().get_or_emplace<GameComponentsComponent>(e);
            for (const GameInstance& item : source.unresolvedGame) {
                if (!game.Find(item.type)) game.items.push_back(item);
            }
        }
    };
    auto command = std::make_unique<AddActorCommand>(ctx.scene, baseName, builder);
    AddActorCommand* raw = command.get();
    ctx.commands.Execute(std::move(command));
    ctx.selected = raw->Id();
    return raw->Id();
}

void SetPropertyCommand(EditorContext& ctx, uint64_t id, const ComponentInfo& info, size_t propIndex,
                        const PropertyValue& value) {
    const entt::entity e = ctx.scene.Find(id);
    if (e == entt::null) return;
    const void* component = info.get(ctx.scene.Registry(), e);
    if (!component) return;
    const PropertyValue before = GetProperty(info.props[propIndex], component);
    if (before == value) return;
    ctx.commands.Execute(std::make_unique<SetPropertyCmd>(ctx.scene, id, info, propIndex, before, value));
}

void RecordPropertyEdit(EditorContext& ctx, uint64_t id, const ComponentInfo& info, size_t propIndex,
                        const PropertyValue& before, const PropertyValue& after) {
    if (before == after) return;
    ctx.commands.PushExecuted(std::make_unique<SetPropertyCmd>(ctx.scene, id, info, propIndex, before, after));
}

void RecordTransformEdit(EditorContext& ctx, uint64_t id, const TransformComponent& before,
                         const TransformComponent& after) {
    if (before.position == after.position && before.rotation == after.rotation && before.scale == after.scale) return;
    ctx.commands.PushExecuted(std::make_unique<TransformCmd>(ctx.scene, id, before, after));
}

void AddComponentCommand(EditorContext& ctx, uint64_t id, const ComponentInfo& info) {
    ctx.commands.Execute(std::make_unique<ComponentCmd>(ctx.scene, id, info, true));
}

void RemoveComponentCommand(EditorContext& ctx, uint64_t id, const ComponentInfo& info) {
    ctx.commands.Execute(std::make_unique<ComponentCmd>(ctx.scene, id, info, false));
}

} // namespace magnum::editor
