#include "game/Gameplay.h"

#include "core/Log.h"
#include "scene/SpriteSize.h"
#include "scene/TransformMath.h"
#include "ui/UiLua.h"
#include "shader/ShaderTypes.h"

#include <GLFW/glfw3.h>
#include <sol/sol.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace magnum {

namespace {

namespace fs = std::filesystem;

// A script that runs this many VM instructions without returning is stopped (an endless loop would freeze the editor).
constexpr int kHookInterval = 100000;
constexpr int kHookLimit = 400; // about 40 million instructions per callback
int g_hookCount = 0;

void InstructionHook(lua_State* L, lua_Debug*) {
    if (++g_hookCount > kHookLimit) luaL_error(L, "the script ran for too long without returning (an endless loop?)");
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const std::unordered_map<std::string, int>& KeyTable() {
    static const std::unordered_map<std::string, int> keys = [] {
        std::unordered_map<std::string, int> k;
        for (char c = 'a'; c <= 'z'; ++c) k[std::string(1, c)] = GLFW_KEY_A + (c - 'a');
        for (char c = '0'; c <= '9'; ++c) k[std::string(1, c)] = GLFW_KEY_0 + (c - '0');
        k["space"] = GLFW_KEY_SPACE;
        k["shift"] = GLFW_KEY_LEFT_SHIFT;
        k["ctrl"] = GLFW_KEY_LEFT_CONTROL;
        k["alt"] = GLFW_KEY_LEFT_ALT;
        k["tab"] = GLFW_KEY_TAB;
        k["enter"] = GLFW_KEY_ENTER;
        k["escape"] = GLFW_KEY_ESCAPE;
        k["up"] = GLFW_KEY_UP;
        k["down"] = GLFW_KEY_DOWN;
        k["left"] = GLFW_KEY_LEFT;
        k["right"] = GLFW_KEY_RIGHT;
        return k;
    }();
    return keys;
}

struct ActorRef {
    uint64_t id = 0;
};

// A handle to the Sprite component of an actor (scripts reach it as actor.sprite).
struct SpriteRef {
    uint64_t id = 0;
};

} // namespace

struct Gameplay::Impl {
    // A script file, run once into its own environment; actors share its functions.
    struct ScriptClass {
        std::string path;
        fs::file_time_type stamp{};
        sol::environment env;
        sol::protected_function onStart, onUpdate, onHit, onTrigger, onTriggerExit;
        bool loaded = false;
    };
    enum Callback { Start, Update, Hit, Trigger, TriggerExit, CallbackCount };
    struct Instance {
        std::shared_ptr<ScriptClass> cls;
        bool started = false;
        bool broken[CallbackCount] = {};
    };

    sol::state lua;
    Scene* scene = nullptr;
    fs::path root;
    Physics physics;
    GameInput input;
    bool running = false;
    bool stopRequested = false;
    bool paused = false; // game.pause(): scripts, physics and animations stand still, the UI keeps running
    double elapsed = 0.0;
    double sinceReloadCheck = 0.0;
    std::vector<HudItem> hud;
    UiSystem ui;
    std::unique_ptr<UiScripting> uiScripting;

    std::map<std::string, std::shared_ptr<ScriptClass>> classes;
    std::unordered_map<uint64_t, Instance> instances;
    std::unordered_map<uint64_t, sol::table> states;
    std::vector<uint64_t> destroyQueue;
    std::unordered_set<uint64_t> destroyed;

    std::unordered_map<int, bool> keyNow, keyBefore;

    // ---- setup ----------------------------------------------------------------------------------------------------

    void OpenLua();
    void BindVectors();
    void BindActor();
    void BindSprite();
    void BindGlobals();

    // ---- scripts --------------------------------------------------------------------------------------------------

    std::shared_ptr<ScriptClass> LoadClass(const std::string& path);
    bool RunClass(ScriptClass& cls, std::string& error);
    void CreateInstance(uint64_t id, const std::string& path);
    template <class... Args>
    void Call(uint64_t id, Callback which, Args&&... args);
    void Reload();
    void Flush();

    SpriteComponent* Sprite(uint64_t id) { return scene && !destroyed.contains(id) ? scene->TryGet<SpriteComponent>(id) : nullptr; }
    TransformComponent* Transform(uint64_t id) { return scene && !destroyed.contains(id) ? scene->TryGet<TransformComponent>(id) : nullptr; }
    bool Alive(uint64_t id) const { return scene && scene->Exists(id) && !destroyed.contains(id); }
    int KeyCode(const std::string& name);
    uint64_t SpawnActor(const std::string& kind, const glm::vec3& at, const std::string& name);
};

// ---- Lua bindings ---------------------------------------------------------------------------------------------------

void Gameplay::Impl::OpenLua() {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    // No file, OS or module access, and no way to load other code behind the sandbox.
    for (const char* name : {"dofile", "loadfile", "load", "loadstring", "require", "collectgarbage"}) lua[name] = sol::nil;
    lua.set_function("print", [](sol::variadic_args args, sol::this_state ts) {
        std::string text;
        for (auto a : args) {
            if (!text.empty()) text += "\t";
            text += luaL_tolstring(ts.L, a.stack_index(), nullptr);
            lua_pop(ts.L, 1);
        }
        Log::Info("[script] {}", text);
    });
    lua["log"] = lua["print"];
    lua_sethook(lua.lua_state(), InstructionHook, LUA_MASKCOUNT, kHookInterval);
}

void Gameplay::Impl::BindVectors() {
    lua.new_usertype<glm::vec3>(
        "Vec3", sol::no_constructor,
        "x", sol::property([](const glm::vec3& v) { return v.x; }, [](glm::vec3& v, float f) { v.x = f; }),
        "y", sol::property([](const glm::vec3& v) { return v.y; }, [](glm::vec3& v, float f) { v.y = f; }),
        "z", sol::property([](const glm::vec3& v) { return v.z; }, [](glm::vec3& v, float f) { v.z = f; }),
        sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
        sol::meta_function::multiplication,
        sol::overload([](const glm::vec3& a, float s) { return a * s; }, [](float s, const glm::vec3& a) { return a * s; },
                      [](const glm::vec3& a, const glm::vec3& b) { return a * b; }),
        sol::meta_function::division, [](const glm::vec3& a, float s) { return a / s; },
        sol::meta_function::unary_minus, [](const glm::vec3& a) { return -a; },
        sol::meta_function::equal_to, [](const glm::vec3& a, const glm::vec3& b) { return a == b; },
        sol::meta_function::to_string, [](const glm::vec3& v) { return "vec3(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")"; },
        "length", [](const glm::vec3& v) { return glm::length(v); },
        "normalized", [](const glm::vec3& v) { return glm::dot(v, v) > 1e-12f ? glm::normalize(v) : glm::vec3(0.0f); },
        "dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
        "cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); },
        "distance", [](const glm::vec3& a, const glm::vec3& b) { return glm::distance(a, b); },
        "lerp", [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); });
    lua.set_function("vec3", sol::overload([]() { return glm::vec3(0.0f); }, [](float s) { return glm::vec3(s); },
                                           [](float x, float y, float z) { return glm::vec3(x, y, z); }));
}

void Gameplay::Impl::BindActor() {
    auto transform = [this](const ActorRef& a) { return Transform(a.id); };

    lua.new_usertype<ActorRef>(
        "Actor", sol::no_constructor,
        "id", sol::readonly_property([](const ActorRef& a) { return static_cast<double>(a.id); }),
        "valid", sol::readonly_property([this](const ActorRef& a) { return Alive(a.id); }),
        "name", sol::property(
            [this](const ActorRef& a) -> std::string {
                const NameComponent* n = Alive(a.id) ? scene->TryGet<NameComponent>(a.id) : nullptr;
                return n ? n->name : std::string();
            },
            [this](const ActorRef& a, const std::string& name) {
                if (NameComponent* n = Alive(a.id) ? scene->TryGet<NameComponent>(a.id) : nullptr) n->name = name;
            }),
        "position", sol::property(
            [transform](const ActorRef& a) { const TransformComponent* t = transform(a); return t ? t->position : glm::vec3(0.0f); },
            [transform](const ActorRef& a, const glm::vec3& v) { if (TransformComponent* t = transform(a)) t->position = v; }),
        "rotation", sol::property(
            [transform](const ActorRef& a) { const TransformComponent* t = transform(a); return t ? t->rotation : glm::vec3(0.0f); },
            [transform](const ActorRef& a, const glm::vec3& v) { if (TransformComponent* t = transform(a)) t->rotation = v; }),
        "scale", sol::property(
            [transform](const ActorRef& a) { const TransformComponent* t = transform(a); return t ? t->scale : glm::vec3(1.0f); },
            [transform](const ActorRef& a, const glm::vec3& v) { if (TransformComponent* t = transform(a)) t->scale = v; }),
        "forward", sol::readonly_property([transform](const ActorRef& a) { const TransformComponent* t = transform(a); return t ? t->Forward() : glm::vec3(0, 0, -1); }),
        "right", sol::readonly_property([transform](const ActorRef& a) {
            const TransformComponent* t = transform(a);
            return t ? glm::normalize(glm::vec3(t->RotationMatrix() * glm::vec4(1, 0, 0, 0))) : glm::vec3(1, 0, 0);
        }),
        "up", sol::readonly_property([transform](const ActorRef& a) {
            const TransformComponent* t = transform(a);
            return t ? glm::normalize(glm::vec3(t->RotationMatrix() * glm::vec4(0, 1, 0, 0))) : glm::vec3(0, 1, 0);
        }),
        "velocity", sol::property([this](const ActorRef& a) { return physics.Velocity(a.id); },
                                  [this](const ActorRef& a, const glm::vec3& v) { if (Alive(a.id)) physics.SetVelocity(a.id, v); }),
        "grounded", sol::readonly_property([this](const ActorRef& a) { return physics.Grounded(a.id); }),
        "material", sol::property(
            [this](const ActorRef& a) -> std::string {
                const MeshComponent* m = Alive(a.id) ? scene->TryGet<MeshComponent>(a.id) : nullptr;
                return m ? m->material : std::string();
            },
            [this](const ActorRef& a, const std::string& path) {
                if (MeshComponent* m = Alive(a.id) ? scene->TryGet<MeshComponent>(a.id) : nullptr) m->material = path;
            }),
        "state", sol::readonly_property([this](const ActorRef& a, sol::this_state ts) {
            auto it = states.find(a.id);
            if (it == states.end()) it = states.emplace(a.id, sol::state_view(ts).create_table()).first;
            return it->second;
        }),
        "sprite", sol::readonly_property([this](const ActorRef& a) -> sol::optional<SpriteRef> {
            if (!Sprite(a.id)) return sol::nullopt;
            return SpriteRef{a.id};
        }),
        "add_sprite", [this](const ActorRef& a, sol::optional<std::string> image, sol::optional<float> width, sol::optional<float> height) {
            if (!Alive(a.id)) return;
            SpriteComponent s;
            s.texture = image.value_or("");
            s.width = width.value_or(0.0f);
            s.height = height.value_or(0.0f);
            scene->Registry().emplace_or_replace<SpriteComponent>(scene->Find(a.id), s);
        },
        "destroy", [this](const ActorRef& a) {
            if (Alive(a.id)) {
                destroyed.insert(a.id);
                destroyQueue.push_back(a.id);
            }
        },
        "move", [transform](const ActorRef& a, const glm::vec3& delta) { if (TransformComponent* t = transform(a)) t->position += delta; },
        "look_at", [transform](const ActorRef& a, const glm::vec3& target) {
            TransformComponent* t = transform(a);
            if (!t) return;
            const glm::vec3 d = target - t->position;
            if (glm::dot(d, d) < 1e-10f) return;
            const glm::vec3 n = glm::normalize(d);
            t->rotation.y = glm::degrees(std::atan2(-n.x, -n.z));
            t->rotation.x = glm::degrees(std::asin(std::clamp(n.y, -1.0f, 1.0f)));
        },
        "add_collider", [this](const ActorRef& a, sol::optional<std::string> shape, sol::optional<bool> trigger) {
            if (!Alive(a.id)) return;
            ColliderComponent c;
            c.shape = shape && Lower(*shape) == "sphere" ? ColliderShape::Sphere : ColliderShape::Box;
            c.trigger = trigger.value_or(false);
            scene->Registry().emplace_or_replace<ColliderComponent>(scene->Find(a.id), c);
        },
        "add_rigidbody", [this](const ActorRef& a) {
            if (Alive(a.id)) scene->Registry().emplace_or_replace<RigidbodyComponent>(scene->Find(a.id));
        },
        "add_script", [this](const ActorRef& a, const std::string& path) {
            if (!Alive(a.id)) return;
            scene->Registry().emplace_or_replace<ScriptComponent>(scene->Find(a.id), ScriptComponent{path});
            CreateInstance(a.id, path);
            Call(a.id, Start, ActorRef{a.id});
        },
        sol::meta_function::equal_to, [](const ActorRef& a, const ActorRef& b) { return a.id == b.id; },
        sol::meta_function::to_string, [this](const ActorRef& a) {
            const NameComponent* n = scene ? scene->TryGet<NameComponent>(a.id) : nullptr;
            return "Actor(" + (n ? n->name : std::string("destroyed")) + ")";
        });
}

void Gameplay::Impl::BindSprite() {
    auto get = [this](const SpriteRef& r) { return Sprite(r.id); };
    lua.new_usertype<SpriteRef>(
        "Sprite", sol::no_constructor,
        "texture", sol::property(
            [get](const SpriteRef& r) { const SpriteComponent* s = get(r); return s ? s->texture : std::string(); },
            [get](const SpriteRef& r, const std::string& v) { if (SpriteComponent* s = get(r)) s->texture = v; }),
        "frame", sol::property(
            [get](const SpriteRef& r) { const SpriteComponent* s = get(r); return s ? s->frame : 0; },
            [get](const SpriteRef& r, int v) { if (SpriteComponent* s = get(r)) { s->frame = v; s->animate = false; } }),
        "tint", sol::property(
            [get](const SpriteRef& r) { const SpriteComponent* s = get(r); return s ? s->tint : glm::vec3(1.0f); },
            [get](const SpriteRef& r, const glm::vec3& v) { if (SpriteComponent* s = get(r)) s->tint = v; }),
        "opacity", sol::property(
            [get](const SpriteRef& r) { const SpriteComponent* s = get(r); return s ? s->opacity : 0.0f; },
            [get](const SpriteRef& r, float v) { if (SpriteComponent* s = get(r)) s->opacity = std::clamp(v, 0.0f, 1.0f); }),
        "flip_x", sol::property(
            [get](const SpriteRef& r) { const SpriteComponent* s = get(r); return s && s->flipX; },
            [get](const SpriteRef& r, bool v) { if (SpriteComponent* s = get(r)) s->flipX = v; }),
        "flip_y", sol::property(
            [get](const SpriteRef& r) { const SpriteComponent* s = get(r); return s && s->flipY; },
            [get](const SpriteRef& r, bool v) { if (SpriteComponent* s = get(r)) s->flipY = v; }),
        "order", sol::property(
            [get](const SpriteRef& r) { const SpriteComponent* s = get(r); return s ? s->order : 0; },
            [get](const SpriteRef& r, int v) { if (SpriteComponent* s = get(r)) s->order = v; }),
        "playing", sol::readonly_property([get](const SpriteRef& r) { const SpriteComponent* s = get(r); return s && s->animate; }),
        // sprite:play(first, last, fps[, loop]): cells first..last of the atlas, restarting only when the clip changes.
        "play", [get](const SpriteRef& r, int first, int last, float fps, sol::optional<bool> loop) {
            SpriteComponent* s = get(r);
            if (!s) return;
            const int count = std::max(1, last - first + 1);
            const bool same = s->animate && s->firstFrame == first && s->frameCount == count && s->loop == loop.value_or(true);
            s->firstFrame = first;
            s->frameCount = count;
            s->fps = fps;
            s->loop = loop.value_or(true);
            if (!same) {
                s->animationTime = 0.0f;
                s->frame = first;
            }
            s->animate = true;
        },
        "stop", [get](const SpriteRef& r) { if (SpriteComponent* s = get(r)) s->animate = false; });
}

void Gameplay::Impl::BindGlobals() {
    // input
    sol::table inputTable = lua.create_named_table("input");
    inputTable["key"] = [this](const std::string& name) {
        const int code = KeyCode(name);
        const auto it = keyNow.find(code);
        return it != keyNow.end() && it->second;
    };
    inputTable["pressed"] = [this](const std::string& name) {
        const int code = KeyCode(name);
        const auto now = keyNow.find(code);
        const auto before = keyBefore.find(code);
        return now != keyNow.end() && now->second && !(before != keyBefore.end() && before->second);
    };
    // A press that landed on a UI element belongs to the UI, not to the game.
    inputTable["mouse"] = [this](int button) { return input.mouseDown && input.mouseDown(button) && !ui.PointerOverUi() && !ui.PressedNode(); };
    inputTable["ui_blocked"] = [this]() { return ui.PointerOverUi() || ui.PressedNode() != 0; };
    inputTable["mouse_delta"] = [this]() { return std::make_tuple(input.mouseDelta.x, input.mouseDelta.y); };
    // Where the mouse is over the view, 0..1 from the top left; nil while it is outside.
    inputTable["mouse_position"] = [this](sol::this_state ts) -> sol::object {
        if (!input.mouseInView) return sol::nil;
        return sol::make_object(ts, std::make_tuple(input.mousePosition.x, input.mousePosition.y));
    };
    // The point of the plane z = `z` under the mouse (2D games), or nil.
    inputTable["mouse_world"] = [this](sol::optional<float> z, sol::this_state ts) -> sol::object {
        if (!scene || !input.mouseInView) return sol::nil;
        const CameraView camera = FindCamera(*scene);
        if (!camera.valid) return sol::nil;
        const ViewRay ray = CameraRay(camera, input.aspect, glm::vec2(input.mousePosition.x * 2.0f - 1.0f, 1.0f - input.mousePosition.y * 2.0f));
        const float plane = z.value_or(0.0f);
        if (std::abs(ray.direction.z) < 1e-6f) return sol::nil;
        const float t = (plane - ray.origin.z) / ray.direction.z;
        if (t < 0.0f && !camera.orthographic) return sol::nil;
        return sol::make_object(ts, ray.origin + ray.direction * t);
    };

    // world
    sol::table world = lua.create_named_table("world");
    world["find"] = [this](const std::string& name) -> sol::optional<ActorRef> {
        if (!scene) return sol::nullopt;
        for (const uint64_t id : scene->Order()) {
            const NameComponent* n = scene->TryGet<NameComponent>(id);
            if (n && n->name == name && !destroyed.contains(id)) return ActorRef{id};
        }
        return sol::nullopt;
    };
    world["find_all"] = [this](const std::string& prefix, sol::this_state ts) {
        sol::table list = sol::state_view(ts).create_table();
        if (!scene) return list;
        for (const uint64_t id : scene->Order()) {
            const NameComponent* n = scene->TryGet<NameComponent>(id);
            if (n && n->name.rfind(prefix, 0) == 0 && !destroyed.contains(id)) list.add(ActorRef{id});
        }
        return list;
    };
    world["spawn"] = [this](const std::string& kind, float x, float y, float z, sol::optional<std::string> name) -> sol::optional<ActorRef> {
        const uint64_t id = SpawnActor(kind, glm::vec3(x, y, z), name.value_or(""));
        if (!id) return sol::nullopt;
        return ActorRef{id};
    };
    world["spawn_sprite"] = [this](const std::string& image, float x, float y, sol::optional<float> width, sol::optional<float> height,
                                   sol::optional<std::string> name) -> sol::optional<ActorRef> {
        const uint64_t id = SpawnActor("empty", glm::vec3(x, y, 0.0f), name.value_or("Sprite"));
        if (!id) return sol::nullopt;
        SpriteComponent s;
        s.texture = image;
        s.width = width.value_or(0.0f);
        s.height = height.value_or(0.0f);
        scene->Registry().emplace<SpriteComponent>(scene->Find(id), s);
        return ActorRef{id};
    };
    // A decal projected onto the surface at `point` whose normal is `normal` (use the result of world.raycast): bullet holes,
    // scorch marks, splats. `size` is the width and height of the decal, `depth` how far it reaches into the surface.
    world["spawn_decal"] = [this](const std::string& image, const glm::vec3& point, const glm::vec3& normal, sol::optional<float> size,
                                  sol::optional<float> depth, sol::optional<std::string> name) -> sol::optional<ActorRef> {
        const uint64_t id = SpawnActor("empty", point, name.value_or("Decal"));
        if (!id) return sol::nullopt;
        // The decal projects along its local Z: point that axis into the surface and spin it around the normal for variety.
        const glm::vec3 z = glm::dot(normal, normal) > 1e-8f ? -glm::normalize(normal) : glm::vec3(0, -1, 0);
        const glm::vec3 up = std::abs(z.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::vec3 x = glm::normalize(glm::cross(up, z));
        const glm::vec3 y = glm::cross(z, x);
        TransformComponent* t = scene->TryGet<TransformComponent>(id);
        t->rotation = EulerFromRotation(glm::mat3(x, y, z));
        const float s = size.value_or(1.0f);
        t->scale = glm::vec3(s, s, depth.value_or(std::max(0.5f * s, 0.25f)));
        DecalComponent decal;
        decal.texture = image;
        scene->Registry().emplace<DecalComponent>(scene->Find(id), decal);
        return ActorRef{id};
    };
    // The ray through a point of the view (x, y: 0..1 from the top left): returns its origin and direction.
    world["screen_ray"] = [this](float x, float y) -> std::tuple<glm::vec3, glm::vec3> {
        const CameraView camera = scene ? FindCamera(*scene) : CameraView{};
        const ViewRay ray = CameraRay(camera, input.aspect, glm::vec2(x * 2.0f - 1.0f, 1.0f - y * 2.0f));
        return {ray.origin, ray.direction};
    };
    world["destroy"] = [this](const ActorRef& a) {
        if (Alive(a.id)) {
            destroyed.insert(a.id);
            destroyQueue.push_back(a.id);
        }
    };
    world["raycast"] = [this](const glm::vec3& origin, const glm::vec3& direction, sol::optional<float> maxDistance, sol::optional<ActorRef> ignore,
                              sol::this_state ts) -> sol::object {
        Physics::Hit hit;
        if (!scene || !physics.Raycast(*scene, origin, direction, maxDistance.value_or(1000.0f), ignore ? ignore->id : 0, hit)) return sol::nil;
        sol::state_view view(ts);
        sol::table result = view.create_table();
        result["actor"] = ActorRef{hit.actor};
        result["distance"] = hit.distance;
        result["point"] = hit.point;
        result["normal"] = hit.normal;
        return result;
    };
    world["set_gravity"] = [this](float g) { physics.gravity = g; };

    // time, hud, game
    lua["time"] = lua.create_table_with("dt", 0.0, "elapsed", 0.0);
    sol::table hudTable = lua.create_named_table("hud");
    hudTable["text"] = [this](float x, float y, const std::string& text, sol::optional<float> size, sol::optional<float> r, sol::optional<float> g,
                              sol::optional<float> b, sol::optional<float> a) {
        HudItem item;
        item.kind = HudItem::Kind::Text;
        item.x = x;
        item.y = y;
        item.text = text;
        item.size = size.value_or(22.0f);
        item.color = glm::vec4(r.value_or(1.0f), g.value_or(1.0f), b.value_or(1.0f), a.value_or(1.0f));
        hud.push_back(std::move(item));
    };
    hudTable["rect"] = [this](float x, float y, float w, float h, sol::optional<float> r, sol::optional<float> g, sol::optional<float> b,
                              sol::optional<float> a) {
        HudItem item;
        item.kind = HudItem::Kind::Rect;
        item.x = x;
        item.y = y;
        item.w = w;
        item.h = h;
        item.color = glm::vec4(r.value_or(1.0f), g.value_or(1.0f), b.value_or(1.0f), a.value_or(1.0f));
        hud.push_back(std::move(item));
    };
    sol::table game = lua.create_named_table("game");
    game["stop"] = [this]() { stopRequested = true; };
    game["vars"] = lua.create_table(); // a table every script and UI document can read and write
    game["pause"] = [this]() { paused = true; };
    game["resume"] = [this]() { paused = false; };
    game["toggle_pause"] = [this]() { paused = !paused; return paused; };
    game["paused"] = [this]() { return paused; };
}

int Gameplay::Impl::KeyCode(const std::string& name) {
    const auto& keys = KeyTable();
    const auto it = keys.find(Lower(name));
    if (it == keys.end()) {
        static std::set<std::string> warned;
        if (warned.insert(name).second) Log::Warn("[script] unknown key '{}' (use a letter, a digit, space, shift, ctrl, alt, tab, enter, escape, up, down, left, right)", name);
        return -1;
    }
    return it->second;
}

uint64_t Gameplay::Impl::SpawnActor(const std::string& kindName, const glm::vec3& at, const std::string& name) {
    if (!scene) return 0;
    const std::string kind = Lower(kindName);
    int primitive = -1;
    for (int i = 0; i < kPrimitiveCount; ++i) {
        if (Lower(kPrimitiveNames[i]) == kind) primitive = i;
    }
    const bool known = primitive >= 0 || kind == "empty" || kind == "camera" || kind == "light";
    if (!known) {
        Log::Warn("[script] world.spawn: unknown kind '{}' (a primitive name such as cube or sphere, or empty, camera, light)", kindName);
        return 0;
    }
    std::string actorName = name;
    if (actorName.empty()) {
        actorName = kind == "empty" ? "Empty Actor" : (primitive >= 0 ? kPrimitiveNames[primitive] : (kind == "camera" ? "Camera" : "Directional Light"));
    }
    const uint64_t id = scene->CreateActor(scene->UniqueName(actorName));
    entt::registry& reg = scene->Registry();
    const entt::entity e = scene->Find(id);
    reg.get<TransformComponent>(e).position = at;
    if (primitive >= 0) {
        MeshComponent mesh;
        mesh.primitive = static_cast<Primitive>(primitive);
        reg.emplace<MeshComponent>(e, mesh);
    } else if (kind == "camera") {
        reg.emplace<CameraComponent>(e);
    } else if (kind == "light") {
        reg.emplace<LightComponent>(e);
    }
    return id;
}

// ---- scripts ----------------------------------------------------------------------------------------------------------

bool Gameplay::Impl::RunClass(ScriptClass& cls, std::string& error) {
    std::ifstream in(root / cls.path, std::ios::binary);
    if (!in) {
        error = cls.path + ": cannot read the script file";
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    std::string source = text.str();
    StripUtf8Bom(source);

    // Definitions go into the script's own environment, reads fall back to the globals (input, world, vec3...).
    sol::environment env(lua, sol::create, lua.globals());
    sol::load_result chunk = lua.load(source, "@" + cls.path, sol::load_mode::text);
    if (!chunk.valid()) {
        const sol::error e = chunk;
        error = e.what();
        return false;
    }
    sol::protected_function run = chunk;
    sol::set_environment(env, run);
    g_hookCount = 0;
    const sol::protected_function_result result = run();
    if (!result.valid()) {
        const sol::error e = result;
        error = e.what();
        return false;
    }
    auto function = [&](const char* name) {
        sol::object o = env[name];
        return o.get_type() == sol::type::function ? sol::protected_function(o) : sol::protected_function();
    };
    cls.env = env;
    cls.onStart = function("on_start");
    cls.onUpdate = function("on_update");
    cls.onHit = function("on_hit");
    cls.onTrigger = function("on_trigger");
    cls.onTriggerExit = function("on_trigger_exit");
    std::error_code ec;
    cls.stamp = fs::last_write_time(root / cls.path, ec);
    cls.loaded = true;
    return true;
}

std::shared_ptr<Gameplay::Impl::ScriptClass> Gameplay::Impl::LoadClass(const std::string& path) {
    if (const auto it = classes.find(path); it != classes.end()) return it->second;
    auto cls = std::make_shared<ScriptClass>();
    cls->path = path;
    std::string error;
    if (!RunClass(*cls, error)) {
        Log::Error("[script {}] {}", path, error);
        classes[path] = nullptr; // do not try again for every actor
        return nullptr;
    }
    classes[path] = cls;
    return cls;
}

void Gameplay::Impl::CreateInstance(uint64_t id, const std::string& path) {
    if (path.empty()) return;
    const auto cls = LoadClass(path);
    if (!cls) return;
    instances[id] = Instance{cls, false, {}};
}

template <class... Args>
void Gameplay::Impl::Call(uint64_t id, Callback which, Args&&... args) {
    const auto it = instances.find(id);
    if (it == instances.end() || !Alive(id)) return;
    Instance& instance = it->second;
    if (which == Start) {
        if (instance.started) return;
        instance.started = true;
    }
    if (instance.broken[which]) return;
    ScriptClass& cls = *instance.cls;
    sol::protected_function* fn = which == Start ? &cls.onStart : which == Update ? &cls.onUpdate : which == Hit ? &cls.onHit
                                  : which == Trigger ? &cls.onTrigger : &cls.onTriggerExit;
    if (!fn->valid()) return;
    g_hookCount = 0;
    const sol::protected_function_result result = (*fn)(std::forward<Args>(args)...);
    if (!result.valid()) {
        const sol::error e = result;
        static const char* names[] = {"on_start", "on_update", "on_hit", "on_trigger", "on_trigger_exit"};
        Log::Error("[script {}] {}: {}", cls.path, names[which], e.what());
        instance.broken[which] = true; // reported once; it runs again after the script is edited
    }
}

void Gameplay::Impl::Reload() {
    for (auto& [path, cls] : classes) {
        if (!cls) continue; // it failed to load; fixing it takes a new play session
        std::error_code ec;
        const auto stamp = fs::last_write_time(root / path, ec);
        if (ec || stamp == cls->stamp) continue;
        ScriptClass fresh;
        fresh.path = path;
        std::string error;
        if (!RunClass(fresh, error)) {
            cls->stamp = stamp; // do not report it every check
            Log::Error("[script {}] reload failed, the previous version keeps running: {}", path, error);
            continue;
        }
        cls->env = fresh.env;
        cls->onStart = fresh.onStart;
        cls->onUpdate = fresh.onUpdate;
        cls->onHit = fresh.onHit;
        cls->onTrigger = fresh.onTrigger;
        cls->onTriggerExit = fresh.onTriggerExit;
        cls->stamp = fresh.stamp;
        for (auto& [id, instance] : instances) {
            if (instance.cls == cls) std::fill(std::begin(instance.broken), std::end(instance.broken), false);
        }
        Log::Info("[script {}] reloaded", path);
    }
}

void Gameplay::Impl::Flush() {
    for (const uint64_t id : destroyQueue) {
        states.erase(id);
        instances.erase(id);
        physics.Forget(id);
        if (scene && scene->Exists(id)) scene->DestroyActor(id);
    }
    destroyQueue.clear();
    destroyed.clear();
}

// ---- public API ---------------------------------------------------------------------------------------------------------

Gameplay::Gameplay() : m_impl(std::make_unique<Impl>()) {}
Gameplay::~Gameplay() = default;

bool Gameplay::Running() const { return m_impl->running; }
bool Gameplay::StopRequested() const { return m_impl->stopRequested; }
const std::vector<HudItem>& Gameplay::Hud() const { return m_impl->hud; }
Physics& Gameplay::GetPhysics() { return m_impl->physics; }
UiSystem& Gameplay::Ui() { return m_impl->ui; }

bool Gameplay::RunLua(const std::string& source, std::string& error) {
    Impl& g = *m_impl;
    if (!g.running) {
        error = "the game is not running";
        return false;
    }
    g_hookCount = 0;
    const sol::protected_function_result r = g.lua.safe_script(source, sol::script_pass_on_error, "=console");
    if (!r.valid()) {
        const sol::error e = r;
        error = e.what();
        return false;
    }
    return true;
}
size_t Gameplay::ScriptCount() const { return m_impl->instances.size(); }

bool Gameplay::Begin(Scene& scene, const fs::path& projectRoot) {
    End();
    Impl& g = *m_impl;
    g.scene = &scene;
    g.root = projectRoot;
    g.running = true;
    g.stopRequested = false;
    g.paused = false;
    g.elapsed = 0.0;
    g.sinceReloadCheck = 0.0;
    g.hud.clear();
    g.keyNow.clear();
    g.keyBefore.clear();

    g.uiScripting.reset(); // its callbacks live in the old VM and must go before it does
    g.ui.ClearCanvases();
    g.lua = sol::state(); // a fresh VM for every play session
    g.OpenLua();
    g.BindVectors();
    g.BindActor();
    g.BindSprite();
    g.BindGlobals();

    g.physics.Begin(scene);

    // The UI: a fresh system and bindings, then one canvas per Canvas component before any script starts.
    g.uiScripting.reset();
    g.ui.ClearCanvases();
    g.uiScripting = std::make_unique<UiScripting>(g.lua.lua_state(), g.ui, projectRoot, [] { g_hookCount = 0; });
    g.uiScripting->Bind();
    g.ui.SetViewSize(static_cast<int>(g.input.viewSize.x), static_cast<int>(g.input.viewSize.y));
    g.uiScripting->SyncSceneCanvases(scene, 0.0f);

    const std::vector<uint64_t> order = scene.Order();
    for (const uint64_t id : order) {
        if (const ScriptComponent* s = scene.TryGet<ScriptComponent>(id)) g.CreateInstance(id, s->script);
    }
    for (const uint64_t id : order) g.Call(id, Impl::Start, ActorRef{id});
    return true;
}

void Gameplay::Update(Scene& scene, float dt, const GameInput& input) {
    Impl& g = *m_impl;
    if (!g.running) return;
    g.scene = &scene;
    g.input = input;
    if (!g.paused) g.hud.clear(); // while paused the last HUD stays on screen
    if (!g.paused) g.elapsed += dt;
    g.lua["time"]["dt"] = dt;
    g.lua["time"]["elapsed"] = g.elapsed;

    // Key state: the keys scripts can name, polled once per frame.
    g.keyBefore = g.keyNow;
    for (const auto& [name, code] : KeyTable()) g.keyNow[code] = input.keyDown && input.keyDown(code);

    g.sinceReloadCheck += dt;
    if (g.sinceReloadCheck > 0.5) {
        g.sinceReloadCheck = 0.0;
        g.Reload();
    }

    if (!g.paused) {
        for (auto [e, sprite] : scene.Registry().view<SpriteComponent>().each()) AdvanceSpriteAnimation(sprite, dt);
    }

    // The UI first, so a click is handled before the scripts of this frame look at the input.
    if (g.uiScripting) {
        UiPointer pointer;
        pointer.position = input.mousePosition * input.viewSize;
        pointer.inView = input.mouseInView;
        for (int b = 0; b < 3; ++b) pointer.down[b] = input.mouseDown && input.mouseDown(b);
        pointer.wheel = input.mouseInView ? input.mouseWheel : 0.0f;
        g.ui.SetViewSize(static_cast<int>(std::max(input.viewSize.x, 1.0f)), static_cast<int>(std::max(input.viewSize.y, 1.0f)));
        g.uiScripting->SyncSceneCanvases(scene, dt);
        g.ui.Update(dt, pointer);
        g.uiScripting->Pump(dt);
    }
    if (g.paused) {
        g.Flush();
        return;
    }

    const std::vector<uint64_t> order = scene.Order();
    for (const uint64_t id : order) g.Call(id, Impl::Update, ActorRef{id}, dt);

    g.physics.Step(scene, dt);

    for (const auto& [a, b] : g.physics.Hits()) {
        g.Call(a, Impl::Hit, ActorRef{a}, ActorRef{b});
        g.Call(b, Impl::Hit, ActorRef{b}, ActorRef{a});
    }
    for (const auto& [a, b] : g.physics.TriggerEnters()) {
        g.Call(a, Impl::Trigger, ActorRef{a}, ActorRef{b});
        g.Call(b, Impl::Trigger, ActorRef{b}, ActorRef{a});
    }
    for (const auto& [a, b] : g.physics.TriggerExits()) {
        g.Call(a, Impl::TriggerExit, ActorRef{a}, ActorRef{b});
        g.Call(b, Impl::TriggerExit, ActorRef{b}, ActorRef{a});
    }
    g.Flush();
}

void Gameplay::End() {
    Impl& g = *m_impl;
    g.running = false;
    g.instances.clear();
    g.states.clear();
    g.classes.clear();
    g.destroyQueue.clear();
    g.destroyed.clear();
    g.hud.clear();
    g.uiScripting.reset();
    g.ui.ClearCanvases();
    g.scene = nullptr;
}

int Gameplay::KeyCodeFromName(const std::string& name) {
    const auto& keys = KeyTable();
    const auto it = keys.find(Lower(name));
    return it == keys.end() ? -1 : it->second;
}

CameraView Gameplay::FindCamera(const Scene& scene) {
    CameraView view;
    const entt::registry& reg = scene.Registry();
    for (const auto [e, camera, transform] : reg.view<const CameraComponent, const TransformComponent>().each()) {
        if (view.valid && !camera.primary) continue; // the first camera is the fallback, a primary one replaces it
        view.valid = true;
        view.position = transform.position;
        view.rotation = transform.rotation;
        view.fov = camera.fov;
        view.nearPlane = camera.nearPlane;
        view.farPlane = camera.farPlane;
        view.orthographic = camera.orthographic;
        view.orthoSize = camera.orthoSize;
        if (camera.primary) break;
    }
    return view;
}

ViewRay CameraRay(const CameraView& camera, float aspect, glm::vec2 ndc) {
    TransformComponent pose;
    pose.position = camera.position;
    pose.rotation = camera.rotation;
    const glm::mat3 axes(pose.RotationMatrix());
    const glm::vec3 right = axes[0], up = axes[1], forward = -axes[2];
    ViewRay ray;
    if (camera.orthographic) {
        ray.origin = camera.position + right * (ndc.x * camera.orthoSize * aspect) + up * (ndc.y * camera.orthoSize);
        ray.direction = forward;
    } else {
        const float tanHalf = std::tan(glm::radians(camera.fov) * 0.5f);
        ray.origin = camera.position;
        ray.direction = glm::normalize(forward + right * (ndc.x * tanHalf * aspect) + up * (ndc.y * tanHalf));
    }
    return ray;
}

} // namespace magnum
