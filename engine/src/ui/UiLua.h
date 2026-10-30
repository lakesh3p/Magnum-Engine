#pragma once

#include "ui/UiSystem.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

struct lua_State;

namespace magnum {

class Scene;

// The Lua face of the UI: the `ui` global of a script state. Scripts and documents (Content/UI/*.lua) build nodes with
// ui.panel{...}, ui.text{...}, ui.button{...} and so on, read and set their properties, animate them and receive
// events as callbacks. The full reference is in the header comment of UiLua.cpp and in docs/UI.md.
class UiScripting {
public:
    // `resetBudget` runs before every call into Lua so a callback that never returns can be stopped.
    UiScripting(lua_State* L, UiSystem& system, std::filesystem::path projectRoot, std::function<void()> resetBudget);
    ~UiScripting();
    UiScripting(const UiScripting&) = delete;
    UiScripting& operator=(const UiScripting&) = delete;

    // Adds the `ui` table to the state.
    void Bind();

    // Runs a document into the canvas `canvasName` (created when missing). On an error nothing changes and false comes
    // back with the message in the log (or in `error`).
    bool LoadDocument(const std::string& path, const std::string& canvasName, int order, std::string* error = nullptr);

    // Keeps one canvas per Canvas component of the scene: created, reloaded when its document changed, removed with
    // its actor. Cheap to call every frame (files are looked at every half second).
    void SyncSceneCanvases(Scene& scene, float dt);

    // Documents whose files changed are loaded again (scripts that built a canvas themselves are not touched).
    void ReloadChanged();

    // Calls the Lua callbacks for what happened in the last UiSystem::Update (clicks, changes, finished tweens) and
    // every node's on_update.
    void Pump(float dt);

    // Forgets every callback (the state is going away or restarting).
    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace magnum
