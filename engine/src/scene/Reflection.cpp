#include "scene/Reflection.h"

#include <iterator>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace magnum {

namespace {

template <class T>
ComponentInfo MakeComponent(const char* name, bool removable, bool hidden, std::vector<PropertyInfo> props) {
    static const T defaults{};

    ComponentInfo c;
    c.name = name;
    c.removable = removable;
    c.hidden = hidden;
    c.props = std::move(props);
    c.has = [](const entt::registry& r, entt::entity e) { return r.all_of<T>(e); };
    c.get = [](entt::registry& r, entt::entity e) -> void* { return r.try_get<T>(e); };
    c.add = [](entt::registry& r, entt::entity e) { r.emplace_or_replace<T>(e); };
    c.remove = [](entt::registry& r, entt::entity e) { r.remove<T>(e); };
    c.clone = [](const entt::registry& r, entt::entity e) -> std::shared_ptr<void> {
        return std::make_shared<T>(r.get<T>(e));
    };
    c.restore = [](entt::registry& r, entt::entity e, const void* p) {
        r.emplace_or_replace<T>(e, *static_cast<const T*>(p));
    };
    c.defaults = &defaults;
    return c;
}

PropertyInfo Prop(const char* name, const char* category, PropKind kind, size_t offset) {
    PropertyInfo p;
    p.name = name;
    p.category = category;
    p.kind = kind;
    p.offset = offset;
    return p;
}

std::vector<ComponentInfo> BuildInfos() {
    std::vector<ComponentInfo> infos;

    infos.push_back(MakeComponent<NameComponent>(
        "Name", false, true, {Prop("Name", "Actor", PropKind::String, offsetof(NameComponent, name))}));

    infos.push_back(MakeComponent<TransformComponent>(
        "Transform", false, false,
        {Prop("Position", "Transform", PropKind::Vec3, offsetof(TransformComponent, position)).Speed(0.05f),
         Prop("Rotation", "Transform", PropKind::Vec3, offsetof(TransformComponent, rotation)).Speed(0.5f),
         Prop("Scale", "Transform", PropKind::Vec3, offsetof(TransformComponent, scale)).Speed(0.02f)}));

    infos.push_back(MakeComponent<MeshComponent>(
        "Mesh", true, false,
        {Prop("Primitive", "Mesh", PropKind::Enum, offsetof(MeshComponent, primitive))
             .Options(std::vector<const char*>(std::begin(kPrimitiveNames), std::end(kPrimitiveNames))),
         Prop("Model", "Mesh", PropKind::String, offsetof(MeshComponent, model)),
         Prop("Material", "Material", PropKind::String, offsetof(MeshComponent, material))}));

    infos.push_back(MakeComponent<LightComponent>(
        "Directional Light", true, false,
        {Prop("Color", "Light", PropKind::Color3, offsetof(LightComponent, color)),
         Prop("Intensity", "Light", PropKind::Float, offsetof(LightComponent, intensity)).Range(0.0f, 10.0f)}));

    infos.push_back(MakeComponent<CameraComponent>(
        "Camera", true, false,
        {Prop("Field of view", "Camera", PropKind::Float, offsetof(CameraComponent, fov)).Range(10.0f, 140.0f),
         Prop("Near plane", "Camera", PropKind::Float, offsetof(CameraComponent, nearPlane)).Range(0.01f, 10.0f).Speed(0.01f),
         Prop("Far plane", "Camera", PropKind::Float, offsetof(CameraComponent, farPlane)).Range(10.0f, 10000.0f).Speed(5.0f),
         Prop("Primary", "Camera", PropKind::Bool, offsetof(CameraComponent, primary)),
         Prop("Orthographic", "Camera", PropKind::Bool, offsetof(CameraComponent, orthographic)),
         Prop("Ortho size", "Camera", PropKind::Float, offsetof(CameraComponent, orthoSize)).Range(0.1f, 500.0f).Speed(0.05f)}));

    infos.push_back(MakeComponent<ColliderComponent>(
        "Collider", true, false,
        {Prop("Shape", "Collider", PropKind::Enum, offsetof(ColliderComponent, shape))
             .Options(std::vector<const char*>(std::begin(kColliderShapeNames), std::end(kColliderShapeNames))),
         Prop("Size", "Collider", PropKind::Vec3, offsetof(ColliderComponent, size)).Speed(0.02f),
         Prop("Trigger", "Collider", PropKind::Bool, offsetof(ColliderComponent, trigger))}));

    infos.push_back(MakeComponent<RigidbodyComponent>(
        "Rigidbody", true, false,
        {Prop("Use gravity", "Rigidbody", PropKind::Bool, offsetof(RigidbodyComponent, useGravity)),
         Prop("Gravity scale", "Rigidbody", PropKind::Float, offsetof(RigidbodyComponent, gravityScale)).Range(-5.0f, 5.0f).Speed(0.05f),
         Prop("Drag", "Rigidbody", PropKind::Float, offsetof(RigidbodyComponent, drag)).Range(0.0f, 20.0f).Speed(0.05f),
         Prop("Kinematic", "Rigidbody", PropKind::Bool, offsetof(RigidbodyComponent, kinematic)),
         Prop("Lock Z", "Rigidbody", PropKind::Bool, offsetof(RigidbodyComponent, lockZ))}));

    {
        PropertyInfo animationTime = Prop("Animation time", "Animation", PropKind::Float, offsetof(SpriteComponent, animationTime));
        animationTime.hidden = true; // runtime state
        infos.push_back(MakeComponent<SpriteComponent>(
            "Sprite", true, false,
            {Prop("Texture", "Sprite", PropKind::String, offsetof(SpriteComponent, texture)),
             Prop("Tint", "Sprite", PropKind::Color3, offsetof(SpriteComponent, tint)),
             Prop("Opacity", "Sprite", PropKind::Float, offsetof(SpriteComponent, opacity)).Range(0.0f, 1.0f).Speed(0.01f),
             Prop("Width", "Size", PropKind::Float, offsetof(SpriteComponent, width)).Range(0.0f, 1000.0f).Speed(0.01f),
             Prop("Height", "Size", PropKind::Float, offsetof(SpriteComponent, height)).Range(0.0f, 1000.0f).Speed(0.01f),
             Prop("Pixels per unit", "Size", PropKind::Float, offsetof(SpriteComponent, pixelsPerUnit)).Range(1.0f, 4096.0f).Speed(1.0f),
             Prop("Columns", "Atlas", PropKind::Int, offsetof(SpriteComponent, columns)),
             Prop("Rows", "Atlas", PropKind::Int, offsetof(SpriteComponent, rows)),
             Prop("Frame", "Atlas", PropKind::Int, offsetof(SpriteComponent, frame)),
             Prop("First frame", "Animation", PropKind::Int, offsetof(SpriteComponent, firstFrame)),
             Prop("Frame count", "Animation", PropKind::Int, offsetof(SpriteComponent, frameCount)),
             Prop("FPS", "Animation", PropKind::Float, offsetof(SpriteComponent, fps)).Range(0.0f, 120.0f).Speed(0.1f),
             Prop("Animate", "Animation", PropKind::Bool, offsetof(SpriteComponent, animate)),
             Prop("Loop", "Animation", PropKind::Bool, offsetof(SpriteComponent, loop)),
             animationTime,
             Prop("Flip X", "Sprite", PropKind::Bool, offsetof(SpriteComponent, flipX)),
             Prop("Flip Y", "Sprite", PropKind::Bool, offsetof(SpriteComponent, flipY)),
             Prop("Billboard", "Sprite", PropKind::Bool, offsetof(SpriteComponent, billboard)),
             Prop("Pixelated", "Sprite", PropKind::Bool, offsetof(SpriteComponent, pixelated)),
             Prop("Order", "Sprite", PropKind::Int, offsetof(SpriteComponent, order))}));
    }

    infos.push_back(MakeComponent<DecalComponent>(
        "Decal", true, false,
        {Prop("Texture", "Decal", PropKind::String, offsetof(DecalComponent, texture)),
         Prop("Tint", "Decal", PropKind::Color3, offsetof(DecalComponent, tint)),
         Prop("Opacity", "Decal", PropKind::Float, offsetof(DecalComponent, opacity)).Range(0.0f, 1.0f).Speed(0.01f),
         Prop("Angle fade", "Decal", PropKind::Float, offsetof(DecalComponent, angleFade)).Range(0.0f, 1.0f).Speed(0.01f),
         Prop("Material", "Decal", PropKind::String, offsetof(DecalComponent, material)),
         Prop("Order", "Decal", PropKind::Int, offsetof(DecalComponent, order))}));

    infos.push_back(MakeComponent<CanvasComponent>(
        "Canvas", true, false,
        {Prop("Document", "Canvas", PropKind::String, offsetof(CanvasComponent, document)),
         Prop("Order", "Canvas", PropKind::Int, offsetof(CanvasComponent, order)),
         Prop("Visible", "Canvas", PropKind::Bool, offsetof(CanvasComponent, visible)),
         Prop("Scale mode", "Scale", PropKind::Enum, offsetof(CanvasComponent, scaleMode))
             .Options(std::vector<const char*>(std::begin(kCanvasScaleNames), std::end(kCanvasScaleNames))),
         Prop("Reference width", "Scale", PropKind::Float, offsetof(CanvasComponent, referenceWidth)).Range(100.0f, 8000.0f).Speed(1.0f),
         Prop("Reference height", "Scale", PropKind::Float, offsetof(CanvasComponent, referenceHeight)).Range(100.0f, 8000.0f).Speed(1.0f)}));

    infos.push_back(MakeComponent<ScriptComponent>(
        "Script", true, false, {Prop("Script", "Script", PropKind::String, offsetof(ScriptComponent, script))}));

    return infos;
}

const std::vector<ComponentInfo>& BuiltinInfos() {
    static const std::vector<ComponentInfo> infos = BuildInfos();
    return infos;
}

std::vector<const ComponentInfo*>& GameInfos() {
    static std::vector<const ComponentInfo*> infos;
    return infos;
}

} // namespace

std::vector<const ComponentInfo*> AllComponentInfos() {
    std::vector<const ComponentInfo*> all;
    for (const ComponentInfo& c : BuiltinInfos()) all.push_back(&c);
    for (const ComponentInfo* c : GameInfos()) all.push_back(c);
    return all;
}

void SetGameComponentInfos(std::vector<const ComponentInfo*> infos) {
    GameInfos() = std::move(infos);
}

const ComponentInfo* FindComponentInfo(const std::string& name) {
    for (const ComponentInfo* c : AllComponentInfos()) {
        if (name == c->name) return c;
    }
    return nullptr;
}

PropertyValue GetProperty(const PropertyInfo& p, const void* component) {
    const char* base = static_cast<const char*>(component) + p.offset;
    switch (p.kind) {
    case PropKind::Bool: return *reinterpret_cast<const bool*>(base);
    case PropKind::Int:
    case PropKind::Enum: return *reinterpret_cast<const int*>(base);
    case PropKind::Float: return *reinterpret_cast<const float*>(base);
    case PropKind::Vec3:
    case PropKind::Color3: return *reinterpret_cast<const glm::vec3*>(base);
    case PropKind::String: return *reinterpret_cast<const std::string*>(base);
    case PropKind::CString: {
        const char* text = reinterpret_cast<const char*>(base);
        size_t length = 0;
        while (length < p.capacity && text[length] != '\0') ++length; // never read past the array
        return std::string(text, length);
    }
    }
    return 0;
}

void SetProperty(const PropertyInfo& p, void* component, const PropertyValue& value) {
    char* base = static_cast<char*>(component) + p.offset;
    switch (p.kind) {
    case PropKind::Bool: *reinterpret_cast<bool*>(base) = std::get<bool>(value); break;
    case PropKind::Int:
    case PropKind::Enum: *reinterpret_cast<int*>(base) = std::get<int>(value); break;
    case PropKind::Float: *reinterpret_cast<float*>(base) = std::get<float>(value); break;
    case PropKind::Vec3:
    case PropKind::Color3: *reinterpret_cast<glm::vec3*>(base) = std::get<glm::vec3>(value); break;
    case PropKind::String: *reinterpret_cast<std::string*>(base) = std::get<std::string>(value); break;
    case PropKind::CString: {
        const std::string& text = std::get<std::string>(value);
        if (p.capacity == 0) break;
        const size_t length = std::min(text.size(), p.capacity - 1); // truncate to fit, keep the terminator
        std::memcpy(base, text.data(), length);
        std::memset(base + length, 0, p.capacity - length);
        break;
    }
    }
}

PropertyValue DefaultValue(const ComponentInfo& info, const PropertyInfo& prop) {
    return GetProperty(prop, info.defaults);
}

} // namespace magnum
