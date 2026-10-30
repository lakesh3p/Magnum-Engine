#pragma once

#include "magnum_game_api.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"

#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace magnum {

// Loads the game DLL, turns the component tables it registers into ComponentInfos (so the editor treats them
// like built-in components) and runs the play loop over the scene's game components.
//
// Hot reload: LoadModule() can be called again at any time. The new DLL is loaded first (a failure leaves the
// old one running), then every game component in the scene is saved to JSON by field name, the old DLL is
// unloaded, and the values are applied to the new layouts.
//
// The DLL is always loaded from a numbered copy in the module directory, so the build can overwrite the
// original while a version is loaded.
class GameRuntime {
public:
    GameRuntime();
    ~GameRuntime();
    GameRuntime(const GameRuntime&) = delete;
    GameRuntime& operator=(const GameRuntime&) = delete;

    void SetModuleDirectory(const std::filesystem::path& directory) { m_moduleDir = directory; }

    bool LoadModule(const std::filesystem::path& dll, Scene& scene, std::string& error);
    void UnloadModule(Scene& scene);
    bool IsLoaded() const { return m_module != nullptr; }
    uint64_t Generation() const { return m_generation; } // number of successful loads
    std::vector<std::string> ComponentNames() const;

    // Binds saved-but-unregistered game components in the scene (loaded from a file, or detached by a reload)
    // to the registered types. Call after loading or restoring a scene.
    void ResolvePending(Scene& scene);

    // Play-in-editor. The game callbacks run only between BeginPlay and EndPlay.
    bool BeginPlay(Scene& scene);
    void Update(Scene& scene, float dt);
    void EndPlay(Scene& scene);
    bool Playing() const { return m_playing; }
    // True after game code crashed (access violation, division by zero...). The editor should stop playing;
    // the callbacks stay disabled until the module is reloaded or play starts again.
    bool Faulted() const { return m_fault; }
    const std::string& FaultMessage() const { return m_faultMessage; }

    // Supplies the host's key_down(). Set by the editor.
    std::function<bool(int)> keyDown;

    // Registers component types without a DLL, as if a module had registered them (tests).
    void SetTypesForTest(Scene& scene, const std::vector<MagnumComponentDesc>& descriptions);

private:
    struct Type {
        std::string name;
        MagnumComponentDesc desc{}; // function pointers; `properties` is not kept (copied below)
        std::deque<std::string> strings; // owns every name/category/option the infos point into
        std::vector<uint8_t> defaults;
        ComponentInfo info;
    };
    using TypeList = std::vector<std::unique_ptr<Type>>;

    friend struct HostFunctions;

    std::unique_ptr<Type> BuildType(const MagnumComponentDesc& desc);
    void Register(TypeList& into, const MagnumComponentDesc& desc);
    void Detach(Scene& scene);  // game components -> JSON (using the current types)
    void Commit(Scene& scene, TypeList&& types);
    const Type* FindType(const std::string& name) const;
    void PublishInfos();

    template <class F>
    void Guarded(const std::string& what, F&& call);
    template <class F>
    void ForEachInstance(Scene& scene, F&& each);

    std::filesystem::path m_moduleDir;
    void* m_module = nullptr; // HMODULE
    std::filesystem::path m_loadedCopy;
    MagnumOnUpdateFn m_onUpdate = nullptr;
    MagnumOnUnloadFn m_onUnload = nullptr;
    uint64_t m_generation = 0;

    TypeList m_types;
    TypeList* m_registering = nullptr; // where register_component() writes during Magnum_OnLoad

    Scene* m_scene = nullptr; // set only while game callbacks run
    bool m_playing = false;
    double m_playTime = 0.0;
    bool m_fault = false;
    std::string m_faultMessage;
};

} // namespace magnum
