#include "ui/UiPreview.h"

#include "core/Log.h"
#include "ui/UiLua.h"

#include <sol/sol.hpp>

namespace magnum {

namespace {

// A document or callback that runs this long without returning is stopped (an endless loop would freeze the editor).
int g_hookCount = 0;
void InstructionHook(lua_State* L, lua_Debug*) {
    if (++g_hookCount > 400) luaL_error(L, "the script ran for too long without returning (an endless loop?)");
}

} // namespace

struct UiPreview::Impl {
    sol::state lua;
    UiSystem ui;
    std::unique_ptr<UiScripting> scripting;
    bool open = false;

    // Documents may mention the game's globals inside their callbacks; here those exist but do nothing.
    void OpenLua(const std::filesystem::path& root) {
        scripting.reset();
        ui.ClearCanvases();
        lua = sol::state();
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
        for (const char* name : {"dofile", "loadfile", "load", "loadstring", "require", "collectgarbage"}) lua[name] = sol::nil;
        lua.script(R"LUA(
            local function noop() end
            local function stub() return setmetatable({}, { __index = function() return noop end }) end
            input = stub(); world = stub(); hud = stub(); game = stub()
            time = { dt = 0, elapsed = 0 }
            function vec3(x, y, z) return { x = x or 0, y = y or 0, z = z or 0 } end
            function log() end
            print = log
        )LUA");
        lua_sethook(lua.lua_state(), InstructionHook, LUA_MASKCOUNT, 100000);
        scripting = std::make_unique<UiScripting>(lua.lua_state(), ui, root, [] { g_hookCount = 0; });
        scripting->Bind();
        open = true;
    }
};

UiPreview::UiPreview() : m_impl(std::make_unique<Impl>()) {}
UiPreview::~UiPreview() {
    Close();
}

void UiPreview::Open(const std::filesystem::path& projectRoot) {
    m_impl->OpenLua(projectRoot);
}

void UiPreview::Close() {
    m_impl->scripting.reset();
    m_impl->ui.ClearCanvases();
    m_impl->open = false;
}

bool UiPreview::IsOpen() const { return m_impl->open; }

UiSystem& UiPreview::System() { return m_impl->ui; }

void UiPreview::Update(Scene& scene, float dt, int viewWidth, int viewHeight, const UiPointer& pointer) {
    Impl& g = *m_impl;
    if (!g.open) return;
    g.ui.SetViewSize(viewWidth, viewHeight);
    g.scripting->SyncSceneCanvases(scene, dt);
    g.ui.Update(dt, pointer);
    g.scripting->Pump(dt);
}

bool UiPreview::Run(const std::string& source, std::string& error) {
    Impl& g = *m_impl;
    if (!g.open) {
        error = "the UI preview is not open";
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

std::string UiPreview::Eval(const std::string& expression) {
    Impl& g = *m_impl;
    if (!g.open) return "nil";
    const sol::protected_function_result r = g.lua.safe_script("return tostring(" + expression + ")", sol::script_pass_on_error, "=eval");
    if (!r.valid()) return "nil";
    const sol::object o = r.get<sol::object>();
    return o.get_type() == sol::type::string ? o.as<std::string>() : "nil";
}

} // namespace magnum
