#include "game/GameRuntime.h"

#include "core/Log.h"
#include "scene/GameComponents.h"
#include "scene/SceneSerializer.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>
#include <type_traits>

namespace magnum {

namespace {

// Runs fn(arg) and turns a crash (access violation, divide by zero, C++ exception) into a return value
// instead of taking the editor down. No C++ objects with destructors may live in this function.
bool SehInvoke(void (*fn)(void*), void* arg, unsigned long* code) {
    __try {
        fn(arg);
        return true;
    } __except (*code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <class F>
bool SafeCall(F&& call, unsigned long& code) {
    auto thunk = [](void* p) { (*static_cast<std::remove_reference_t<F>*>(p))(); };
    return SehInvoke(thunk, &call, &code);
}

std::string FormatCode(unsigned long code) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", code);
    return buffer;
}

} // namespace

// The functions handed to the game DLL. They live in a struct so GameRuntime can befriend them.
struct HostFunctions {
    static GameRuntime* active;

    static void Log(int32_t level, const char* message) {
        if (!message) return;
        const LogLevel l = level >= MAGNUM_LOG_ERROR ? LogLevel::Error : (level == MAGNUM_LOG_WARN ? LogLevel::Warn : LogLevel::Info);
        Log::Write(l, std::string("[game] ") + message);
    }

    static void RegisterComponent(const MagnumComponentDesc* desc) {
        if (!active || !active->m_registering || !desc) {
            Log::Warn("[game] register_component is only allowed inside Magnum_OnLoad");
            return;
        }
        active->Register(*active->m_registering, *desc);
    }

    static int32_t GetTransform(MagnumActorId id, MagnumTransform* out) {
        if (!active || !active->m_scene || !out) return 0;
        const TransformComponent* t = active->m_scene->TryGet<TransformComponent>(id);
        if (!t) return 0;
        std::memcpy(out->position, &t->position, sizeof(out->position));
        std::memcpy(out->rotation, &t->rotation, sizeof(out->rotation));
        std::memcpy(out->scale, &t->scale, sizeof(out->scale));
        return 1;
    }

    static int32_t SetTransform(MagnumActorId id, const MagnumTransform* value) {
        if (!active || !active->m_scene || !value) return 0;
        TransformComponent* t = active->m_scene->TryGet<TransformComponent>(id);
        if (!t) return 0;
        std::memcpy(&t->position, value->position, sizeof(value->position));
        std::memcpy(&t->rotation, value->rotation, sizeof(value->rotation));
        std::memcpy(&t->scale, value->scale, sizeof(value->scale));
        return 1;
    }

    static MagnumActorId FindActor(const char* name) {
        if (!active || !active->m_scene || !name) return 0;
        for (const uint64_t id : active->m_scene->Order()) {
            const NameComponent* n = active->m_scene->TryGet<NameComponent>(id);
            if (n && n->name == name) return id;
        }
        return 0;
    }

    static int32_t GetActorName(MagnumActorId id, char* buffer, uint32_t capacity) {
        if (!active || !active->m_scene || !buffer || capacity == 0) return 0;
        const NameComponent* n = active->m_scene->TryGet<NameComponent>(id);
        if (!n) return 0;
        const size_t length = std::min<size_t>(n->name.size(), capacity - 1);
        std::memcpy(buffer, n->name.data(), length);
        buffer[length] = '\0';
        return 1;
    }

    static int32_t KeyDown(int32_t key) {
        return active && active->keyDown && active->keyDown(key) ? 1 : 0;
    }

    static double Time() { return active ? active->m_playTime : 0.0; }
};

GameRuntime* HostFunctions::active = nullptr;

namespace {

const MagnumHostApi kHostApi = {
    MAGNUM_GAME_API_VERSION,
    &HostFunctions::Log,
    &HostFunctions::RegisterComponent,
    &HostFunctions::GetTransform,
    &HostFunctions::SetTransform,
    &HostFunctions::FindActor,
    &HostFunctions::GetActorName,
    &HostFunctions::KeyDown,
    &HostFunctions::Time,
};

} // namespace

GameRuntime::GameRuntime() {
    HostFunctions::active = this;
}

GameRuntime::~GameRuntime() {
    if (m_module) {
        unsigned long code = 0;
        if (m_onUnload) SafeCall([&] { m_onUnload(); }, code);
        FreeLibrary(static_cast<HMODULE>(m_module));
        std::error_code ec;
        std::filesystem::remove(m_loadedCopy, ec);
    }
    SetGameComponentInfos({});
    if (HostFunctions::active == this) HostFunctions::active = nullptr;
}

// ---- Types -------------------------------------------------------------------------------------

std::unique_ptr<GameRuntime::Type> GameRuntime::BuildType(const MagnumComponentDesc& d) {
    if (!d.name || !*d.name || d.size == 0 || d.size > (1u << 20)) {
        Log::Error("[game] a component was registered with an empty name or an invalid size, ignored");
        return nullptr;
    }

    auto t = std::make_unique<Type>();
    t->name = d.name;
    t->desc = d;
    t->desc.name = nullptr;
    t->desc.properties = nullptr;
    t->strings.push_back(t->name);
    const char* typeName = t->strings.back().c_str();

    t->defaults.assign(d.size, 0);
    if (d.construct) {
        unsigned long code = 0;
        uint8_t* data = t->defaults.data();
        if (!SafeCall([&] { d.construct(data); }, code)) {
            Log::Error("[game] {}: construct() crashed ({}), component ignored", t->name, FormatCode(code));
            return nullptr;
        }
    }

    ComponentInfo& info = t->info;
    info.name = typeName;
    info.game = true;
    info.removable = true;
    for (uint32_t i = 0; i < d.property_count && d.properties; ++i) {
        const MagnumPropertyDesc& pd = d.properties[i];
        if (!pd.name || !*pd.name) continue;

        PropertyInfo p;
        size_t size = 0;
        switch (pd.kind) {
        case MAGNUM_PROP_BOOL: p.kind = PropKind::Bool; size = sizeof(bool); break;
        case MAGNUM_PROP_INT: p.kind = PropKind::Int; size = sizeof(int32_t); break;
        case MAGNUM_PROP_FLOAT: p.kind = PropKind::Float; size = sizeof(float); break;
        case MAGNUM_PROP_VEC3: p.kind = PropKind::Vec3; size = 3 * sizeof(float); break;
        case MAGNUM_PROP_COLOR3: p.kind = PropKind::Color3; size = 3 * sizeof(float); break;
        case MAGNUM_PROP_STRING: p.kind = PropKind::CString; size = pd.capacity; p.capacity = pd.capacity; break;
        case MAGNUM_PROP_ENUM: p.kind = PropKind::Enum; size = sizeof(int32_t); break;
        default:
            Log::Warn("[game] {}.{}: unknown property kind {}, skipped", t->name, pd.name, pd.kind);
            continue;
        }
        if (size == 0 || static_cast<size_t>(pd.offset) + size > d.size) {
            Log::Warn("[game] {}.{}: offset/size does not fit in the component, skipped", t->name, pd.name);
            continue;
        }

        t->strings.push_back(pd.name);
        p.name = t->strings.back().c_str();
        t->strings.push_back(pd.category && *pd.category ? pd.category : t->name);
        p.category = t->strings.back().c_str();
        p.offset = pd.offset;
        if (pd.min_value != pd.max_value) {
            p.hasRange = true;
            p.min = pd.min_value;
            p.max = pd.max_value;
        }
        if (pd.speed > 0.0f) p.speed = pd.speed;
        p.hidden = (pd.flags & MAGNUM_PROP_HIDDEN) != 0;
        if (p.kind == PropKind::Enum && pd.options) {
            std::string all = pd.options;
            size_t start = 0;
            while (start <= all.size()) {
                size_t end = all.find('|', start);
                if (end == std::string::npos) end = all.size();
                t->strings.push_back(all.substr(start, end - start));
                p.options.push_back(t->strings.back().c_str());
                start = end + 1;
            }
        }
        info.props.push_back(std::move(p));
    }

    // The component functions work on the instance list of the actor; `type` stays valid as long as the
    // module generation does (infos are replaced together with the types).
    Type* type = t.get();
    info.has = [type](const entt::registry& r, entt::entity e) {
        const auto* gc = r.try_get<GameComponentsComponent>(e);
        const GameInstance* item = gc ? gc->Find(type->name) : nullptr;
        return item && item->resolved;
    };
    info.get = [type](entt::registry& r, entt::entity e) -> void* {
        auto* gc = r.try_get<GameComponentsComponent>(e);
        GameInstance* item = gc ? gc->Find(type->name) : nullptr;
        return item && item->resolved ? static_cast<void*>(item->data.data()) : nullptr;
    };
    info.add = [type](entt::registry& r, entt::entity e) {
        auto& gc = r.get_or_emplace<GameComponentsComponent>(e);
        if (gc.Find(type->name)) return;
        GameInstance item;
        item.type = type->name;
        item.data = type->defaults;
        item.resolved = true;
        gc.items.push_back(std::move(item));
    };
    info.remove = [type](entt::registry& r, entt::entity e) {
        if (auto* gc = r.try_get<GameComponentsComponent>(e)) {
            gc->items.erase(std::remove_if(gc->items.begin(), gc->items.end(),
                                           [&](const GameInstance& i) { return i.type == type->name; }),
                            gc->items.end());
        }
    };
    info.clone = [type](const entt::registry& r, entt::entity e) -> std::shared_ptr<void> {
        const auto* gc = r.try_get<GameComponentsComponent>(e);
        const GameInstance* item = gc ? gc->Find(type->name) : nullptr;
        auto bytes = std::make_shared<std::vector<uint8_t>>(item ? item->data : type->defaults);
        return std::shared_ptr<void>(bytes, bytes->data()); // get() points at the data, the vector stays alive
    };
    info.restore = [type](entt::registry& r, entt::entity e, const void* data) {
        auto& gc = r.get_or_emplace<GameComponentsComponent>(e);
        GameInstance* item = gc.Find(type->name);
        if (!item) {
            gc.items.emplace_back();
            item = &gc.items.back();
            item->type = type->name;
        }
        const auto* bytes = static_cast<const uint8_t*>(data);
        item->data.assign(bytes, bytes + type->defaults.size());
        item->pendingJson.clear();
        item->resolved = true;
    };
    info.defaults = t->defaults.data();
    return t;
}

void GameRuntime::Register(TypeList& into, const MagnumComponentDesc& desc) {
    std::unique_ptr<Type> type = BuildType(desc);
    if (!type) return;
    for (const auto& existing : into) {
        if (existing->name == type->name) {
            Log::Warn("[game] component '{}' is registered twice, the second one is ignored", type->name);
            return;
        }
    }
    into.push_back(std::move(type));
}

const GameRuntime::Type* GameRuntime::FindType(const std::string& name) const {
    for (const auto& t : m_types) {
        if (t->name == name) return t.get();
    }
    return nullptr;
}

void GameRuntime::PublishInfos() {
    std::vector<const ComponentInfo*> infos;
    for (const auto& t : m_types) infos.push_back(&t->info);
    SetGameComponentInfos(std::move(infos));
}

std::vector<std::string> GameRuntime::ComponentNames() const {
    std::vector<std::string> names;
    for (const auto& t : m_types) names.push_back(t->name);
    return names;
}

template <class F>
void GameRuntime::ForEachInstance(Scene& scene, F&& each) {
    for (const uint64_t id : scene.Order()) {
        auto* gc = scene.Registry().try_get<GameComponentsComponent>(scene.Find(id));
        if (!gc) continue;
        for (GameInstance& item : gc->items) each(id, item);
    }
}

void GameRuntime::Detach(Scene& scene) {
    ForEachInstance(scene, [&](uint64_t, GameInstance& item) {
        if (!item.resolved) return;
        if (const Type* type = FindType(item.type)) {
            item.pendingJson = SerializeProperties(type->info, item.data.data());
        }
        item.data.clear();
        item.resolved = false;
    });
}

void GameRuntime::ResolvePending(Scene& scene) {
    std::set<std::string> missing;
    ForEachInstance(scene, [&](uint64_t id, GameInstance& item) {
        if (item.resolved) return;
        const Type* type = FindType(item.type);
        if (!type) {
            missing.insert(item.type);
            return;
        }
        item.data = type->defaults;
        if (!item.pendingJson.empty()) {
            const std::string where = "actor " + std::to_string(id);
            ApplyProperties(type->info, item.data.data(), item.pendingJson, where);
        }
        item.pendingJson.clear();
        item.resolved = true;
    });
    if (IsLoaded()) {
        for (const std::string& name : missing) {
            Log::Warn("Scene uses game component '{}' that the game module does not register; its values are kept as saved", name);
        }
    }
}

// ---- Module loading ----------------------------------------------------------------------------

void GameRuntime::SetTypesForTest(Scene& scene, const std::vector<MagnumComponentDesc>& descriptions) {
    TypeList staging;
    for (const MagnumComponentDesc& d : descriptions) Register(staging, d);
    Detach(scene);
    m_types = std::move(staging);
    PublishInfos();
    ResolvePending(scene);
    ++m_generation;
}

bool GameRuntime::LoadModule(const std::filesystem::path& dll, Scene& scene, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(dll, ec)) {
        error = "game module not found: " + dll.string();
        return false;
    }

    // Load a private copy so the build can overwrite the original while this version is in use.
    std::filesystem::create_directories(m_moduleDir, ec);
    if (!m_generation && !m_module) { // first load of this session: clear copies left by earlier runs
        for (const auto& item : std::filesystem::directory_iterator(m_moduleDir, ec)) {
            if (item.path().filename().string().rfind("MagnumGame_", 0) == 0) std::filesystem::remove(item.path(), ec);
        }
    }
    static int copyCounter = 0;
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count();
    const std::filesystem::path copy = m_moduleDir / ("MagnumGame_" + std::to_string(stamp) + "_" + std::to_string(++copyCounter) + ".dll");
    std::filesystem::copy_file(dll, copy, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "cannot copy the game module: " + ec.message();
        return false;
    }

    HMODULE module = LoadLibraryW(copy.c_str());
    if (!module) {
        error = "LoadLibrary failed (error " + std::to_string(GetLastError()) + ")";
        std::filesystem::remove(copy, ec);
        return false;
    }
    const auto onLoad = reinterpret_cast<MagnumOnLoadFn>(GetProcAddress(module, "Magnum_OnLoad"));
    const auto onUpdate = reinterpret_cast<MagnumOnUpdateFn>(GetProcAddress(module, "Magnum_OnUpdate"));
    const auto onUnload = reinterpret_cast<MagnumOnUnloadFn>(GetProcAddress(module, "Magnum_OnUnload"));
    if (!onLoad) {
        error = "the module does not export Magnum_OnLoad";
        FreeLibrary(module);
        std::filesystem::remove(copy, ec);
        return false;
    }

    // Register into a staging list; the running version stays untouched until this one is known to be good.
    TypeList staging;
    m_registering = &staging;
    int32_t result = -1;
    unsigned long code = 0;
    const bool survived = SafeCall([&] { result = onLoad(&kHostApi); }, code);
    m_registering = nullptr;
    if (!survived || result != 0) {
        error = survived ? "Magnum_OnLoad returned " + std::to_string(result)
                         : "Magnum_OnLoad crashed (" + FormatCode(code) + ")";
        if (survived && onUnload) SafeCall([&] { onUnload(); }, code);
        FreeLibrary(module);
        std::filesystem::remove(copy, ec);
        return false;
    }

    // Commit: save values by name using the old layouts, drop the old module, apply to the new layouts.
    Detach(scene);
    const bool hadModule = m_module != nullptr;
    if (hadModule) {
        if (m_onUnload) SafeCall([&] { m_onUnload(); }, code);
        FreeLibrary(static_cast<HMODULE>(m_module));
        std::filesystem::remove(m_loadedCopy, ec);
    }
    m_types = std::move(staging);
    PublishInfos();
    m_module = module;
    m_loadedCopy = copy;
    m_onUpdate = onUpdate;
    m_onUnload = onUnload;
    ++m_generation;
    m_fault = false;
    m_faultMessage.clear();
    ResolvePending(scene);
    return true;
}

void GameRuntime::UnloadModule(Scene& scene) {
    if (!m_module) return;
    if (m_playing) EndPlay(scene);
    Detach(scene);
    unsigned long code = 0;
    if (m_onUnload) SafeCall([&] { m_onUnload(); }, code);
    FreeLibrary(static_cast<HMODULE>(m_module));
    std::error_code ec;
    std::filesystem::remove(m_loadedCopy, ec);
    m_module = nullptr;
    m_onUpdate = nullptr;
    m_onUnload = nullptr;
    m_types.clear();
    PublishInfos();
}

// ---- Play --------------------------------------------------------------------------------------

template <class F>
void GameRuntime::Guarded(const std::string& what, F&& call) {
    if (m_fault) return;
    unsigned long code = 0;
    if (!SafeCall(call, code)) {
        m_fault = true;
        m_faultMessage = "Game code crashed in " + what + " (exception " + FormatCode(code) + ")";
        Log::Error("{}", m_faultMessage);
    }
}

bool GameRuntime::BeginPlay(Scene& scene) {
    m_fault = false;
    m_faultMessage.clear();
    m_playing = true;
    m_playTime = 0.0;
    m_scene = &scene;
    ForEachInstance(scene, [&](uint64_t id, GameInstance& item) {
        const Type* type = item.resolved ? FindType(item.type) : nullptr;
        if (type && type->desc.on_begin) {
            Guarded(item.type + ".on_begin", [&] { type->desc.on_begin(id, item.data.data()); });
        }
    });
    m_scene = nullptr;
    return !m_fault;
}

void GameRuntime::Update(Scene& scene, float dt) {
    if (!m_playing || m_fault) return;
    m_scene = &scene;
    m_playTime += dt;
    ForEachInstance(scene, [&](uint64_t id, GameInstance& item) {
        const Type* type = item.resolved ? FindType(item.type) : nullptr;
        if (type && type->desc.on_update) {
            Guarded(item.type + ".on_update", [&] { type->desc.on_update(id, item.data.data(), dt); });
        }
    });
    if (m_onUpdate) Guarded("Magnum_OnUpdate", [&] { m_onUpdate(dt); });
    m_scene = nullptr;
}

void GameRuntime::EndPlay(Scene& scene) {
    if (!m_playing) return;
    m_scene = &scene;
    ForEachInstance(scene, [&](uint64_t id, GameInstance& item) {
        const Type* type = item.resolved ? FindType(item.type) : nullptr;
        if (type && type->desc.on_end) {
            Guarded(item.type + ".on_end", [&] { type->desc.on_end(id, item.data.data()); });
        }
    });
    m_scene = nullptr;
    m_playing = false;
}

} // namespace magnum
