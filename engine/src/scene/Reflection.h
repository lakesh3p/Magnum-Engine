#pragma once

#include "scene/Components.h"

#include <entt/entt.hpp>

#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace magnum {

// Components describe their editable properties once, here. The Details panel, undo/redo,
// console `set` command and scene files all work from these descriptions. Built-in components
// are described in Reflection.cpp; components defined by the game DLL are described at runtime
// from the tables the DLL registers (see game/GameRuntime.h).

enum class PropKind { Bool, Int, Float, Vec3, Color3, String, CString, Enum };
using PropertyValue = std::variant<bool, int, float, glm::vec3, std::string>;

struct PropertyInfo {
    const char* name = "";
    const char* category = "";
    PropKind kind = PropKind::Float;
    size_t offset = 0; // byte offset inside the component struct
    bool hasRange = false;
    float min = 0.0f;
    float max = 0.0f;
    float speed = 0.1f; // drag speed
    bool hidden = false; // saved, but not shown in Details
    size_t capacity = 0; // CString: size of the char array including the terminator
    std::vector<const char*> options; // Enum labels; the stored int indexes into this

    PropertyInfo Range(float lo, float hi) const { auto p = *this; p.hasRange = true; p.min = lo; p.max = hi; return p; }
    PropertyInfo Speed(float s) const { auto p = *this; p.speed = s; return p; }
    PropertyInfo Options(std::vector<const char*> o) const { auto p = *this; p.options = std::move(o); return p; }
};

struct ComponentInfo {
    const char* name = "";
    bool removable = true;
    bool hidden = false; // not listed in Details (Name is shown as the title instead)
    bool game = false;   // defined by the game DLL (can disappear when the module is reloaded)
    std::vector<PropertyInfo> props;

    std::function<bool(const entt::registry&, entt::entity)> has;
    std::function<void*(entt::registry&, entt::entity)> get;
    std::function<void(entt::registry&, entt::entity)> add;
    std::function<void(entt::registry&, entt::entity)> remove;
    // clone() returns a copy whose get() points at the component data (laid out like `get` returns it).
    std::function<std::shared_ptr<void>(const entt::registry&, entt::entity)> clone;
    std::function<void(entt::registry&, entt::entity, const void*)> restore;
    const void* defaults = nullptr; // a default-constructed component, for "reset to default"
};

// Built-in components first, then the ones the game module registered. Pointers stay valid until the
// game module is reloaded, so keep component *names* (not pointers) across frames.
std::vector<const ComponentInfo*> AllComponentInfos();
const ComponentInfo* FindComponentInfo(const std::string& name);

// Called by the game runtime when the set of game components changes. The infos are owned by the caller.
void SetGameComponentInfos(std::vector<const ComponentInfo*> infos);

PropertyValue GetProperty(const PropertyInfo& prop, const void* component);
void SetProperty(const PropertyInfo& prop, void* component, const PropertyValue& value);
PropertyValue DefaultValue(const ComponentInfo& info, const PropertyInfo& prop);

} // namespace magnum
