#include "PluginHost.h"

#include "core/Log.h"

#include <imgui.h>
#include <sol/sol.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace magnum::editor {

namespace {

constexpr const char* kPluginFile = "plugin.lua";

struct Plugin {
    std::string name;
    std::string error;
    std::unique_ptr<sol::state> lua;
    int menus = 0, commands = 0, panels = 0;
};

// A callback with a switch: the first error turns it off so a broken panel cannot flood the log every frame.
struct Callback {
    Plugin* plugin = nullptr;
    sol::protected_function fn;
    bool broken = false;
};

struct MenuEntry : Callback {
    std::string menu, label;
};

struct PanelEntry : Callback {
    std::string title;
    bool open = false;        // opened from Tools > Plugins
    bool window = false;      // a floating window that opens at the mouse instead of a dockable panel
    float width = 0.0f;       // window: content width
    bool placeAtMouse = false;
    ImVec2 anchor{0.0f, 0.0f};
};

std::string ReadFile(const std::filesystem::path& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    ok = static_cast<bool>(in);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

} // namespace

struct PluginHost::Impl {
    std::filesystem::path dir;
    PluginServices services;
    std::vector<std::unique_ptr<Plugin>> plugins;
    std::vector<MenuEntry> menus;
    std::map<std::string, Callback> commands;
    std::vector<PanelEntry> panels;
    std::vector<Callback> updates;
    Plugin* loading = nullptr; // the plugin whose script is running right now
    bool reloadRequested = false;

    void Clear() {
        // Callbacks hold references into their Lua states, so they go first.
        menus.clear();
        commands.clear();
        panels.clear();
        updates.clear();
        plugins.clear();
    }

    void Report(const Callback& cb, const sol::error& e, const char* what) {
        Log::Error("[plugin {}] {}: {}", cb.plugin->name, what, e.what());
    }

    // Calls fn; on error reports once and disables the callback. Returns true if it ran fine.
    template <typename... Args>
    bool Call(Callback& cb, const char* what, Args&&... args) {
        if (cb.broken || !cb.fn.valid()) return false;
        sol::protected_function_result r = cb.fn(std::forward<Args>(args)...);
        if (!r.valid()) {
            cb.broken = true;
            Report(cb, r.get<sol::error>(), what);
            return false;
        }
        return true;
    }

    void Bind(Plugin& plugin);
    std::string Load(const std::string& name, const std::string& source);
};

void PluginHost::Impl::Bind(Plugin& plugin) {
    sol::state& lua = *plugin.lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    for (const char* name : {"dofile", "loadfile", "load", "require", "collectgarbage"}) lua[name] = sol::nil;

    Plugin* self = &plugin;
    sol::table magnum = lua.create_named_table("magnum");

    magnum["log"] = [self](const std::string& text) { Log::Info("[{}] {}", self->name, text); };
    magnum["warn"] = [self](const std::string& text) { Log::Warn("[{}] {}", self->name, text); };
    magnum["error"] = [self](const std::string& text) { Log::Error("[{}] {}", self->name, text); };
    magnum["exec"] = [this](const std::string& line) {
        if (services.exec) services.exec(line);
    };
    magnum["selected"] = [this](sol::this_state s) -> sol::object {
        const std::string name = services.selectedName ? services.selectedName() : std::string();
        return name.empty() ? sol::object(sol::nil) : sol::make_object(s, name);
    };
    magnum["mcp_status"] = [this]() { return services.mcpStatus ? services.mcpStatus() : std::string(); };
    magnum["mcp"] = [this](sol::this_state s) {
        sol::state_view view(s);
        const McpInfo info = services.mcpInfo ? services.mcpInfo() : McpInfo{};
        sol::table t = view.create_table();
        t["running"] = info.running;
        t["port"] = info.port;
        t["client"] = info.client;
        t["server_built"] = info.serverBuilt;
        t["server_path"] = info.serverPath;
        return t;
    };
    magnum["actors"] = [this](sol::this_state s) {
        sol::state_view view(s);
        sol::table t = view.create_table();
        if (services.actorNames) {
            for (const std::string& n : services.actorNames()) t.add(n);
        }
        return t;
    };

    magnum["menu"] = [this, self](const std::string& path, sol::protected_function fn) {
        const size_t slash = path.find('/');
        if (slash == std::string::npos || slash == 0 || slash + 1 >= path.size()) {
            throw std::runtime_error("magnum.menu path must look like \"Tools/My Action\"");
        }
        MenuEntry m;
        m.plugin = self;
        m.fn = std::move(fn);
        m.menu = path.substr(0, slash);
        m.label = path.substr(slash + 1);
        menus.push_back(std::move(m));
        ++self->menus;
    };
    magnum["command"] = [this, self](const std::string& name, sol::protected_function fn) {
        if (name.empty() || name.find_first_of(" \t") != std::string::npos) {
            throw std::runtime_error("a command name cannot be empty or contain spaces");
        }
        Callback cb;
        cb.plugin = self;
        cb.fn = std::move(fn);
        commands[name] = std::move(cb);
        ++self->commands;
    };
    magnum["panel"] = [this, self](const std::string& title, sol::protected_function fn, sol::optional<sol::table> options) {
        PanelEntry p;
        p.plugin = self;
        p.fn = std::move(fn);
        p.title = title;
        if (options) {
            p.window = options->get_or("window", false);
            p.width = options->get_or("width", 0.0f);
            p.open = options->get_or("open", false); // shown from the start instead of waiting for Tools > Plugins
        }
        panels.push_back(std::move(p));
        ++self->panels;
    };
    magnum["on_update"] = [this, self](sol::protected_function fn) {
        Callback cb;
        cb.plugin = self;
        cb.fn = std::move(fn);
        updates.push_back(std::move(cb));
    };
}

std::string PluginHost::Impl::Load(const std::string& name, const std::string& source) {
    auto plugin = std::make_unique<Plugin>();
    plugin->name = name;
    plugin->lua = std::make_unique<sol::state>();
    Plugin& p = *plugin;
    plugins.push_back(std::move(plugin));
    Bind(p);

    const std::string chunk = "@Plugins/" + name + "/" + kPluginFile;
    sol::load_result loaded = p.lua->load(source, chunk);
    if (!loaded.valid()) {
        const sol::error e = loaded;
        p.error = e.what();
        Log::Error("[plugin {}] {}", name, p.error);
        return p.error;
    }
    sol::protected_function run = loaded;
    sol::protected_function_result r = run();
    if (!r.valid()) {
        p.error = r.get<sol::error>().what();
        Log::Error("[plugin {}] {}", name, p.error);
        return p.error;
    }
    return {};
}

PluginHost::PluginHost() : m_impl(std::make_unique<Impl>()) {}
PluginHost::~PluginHost() {
    m_impl->Clear();
}

void PluginHost::Init(const std::filesystem::path& pluginsDir, PluginServices services) {
    m_impl->dir = pluginsDir;
    m_impl->services = std::move(services);
    Reload();
}

void PluginHost::Reload() {
    m_impl->Clear();
    std::error_code ec;
    std::vector<std::filesystem::path> dirs;
    for (const auto& entry : std::filesystem::directory_iterator(m_impl->dir, ec)) {
        if (entry.is_directory() && std::filesystem::exists(entry.path() / kPluginFile)) dirs.push_back(entry.path());
    }
    std::sort(dirs.begin(), dirs.end());
    for (const auto& d : dirs) {
        bool ok = false;
        const std::string source = ReadFile(d / kPluginFile, ok);
        if (!ok) continue;
        m_impl->Load(d.filename().string(), source);
    }
    Log::Info("{} plugin(s) loaded from {}", m_impl->plugins.size(), m_impl->dir.generic_string());
}

std::string PluginHost::LoadSource(const std::string& name, const std::string& source) {
    return m_impl->Load(name, source);
}

bool PluginHost::DrawMenuItems(const std::string& menu) {
    bool any = false;
    for (MenuEntry& m : m_impl->menus) {
        if (m.menu != menu) continue;
        if (!any) ImGui::Separator();
        any = true;
        if (ImGui::MenuItem(m.label.c_str(), nullptr, false, !m.broken)) m_impl->Call(m, "menu item");
    }
    return any;
}

std::vector<std::string> PluginHost::ExtraMenus(const std::vector<std::string>& builtIn) const {
    std::vector<std::string> out;
    for (const MenuEntry& m : m_impl->menus) {
        if (std::find(builtIn.begin(), builtIn.end(), m.menu) == builtIn.end() &&
            std::find(out.begin(), out.end(), m.menu) == out.end()) {
            out.push_back(m.menu);
        }
    }
    return out;
}

void PluginHost::DrawPanels() {
    int index = 0;
    for (PanelEntry& p : m_impl->panels) {
        const float cascade = 310.0f * static_cast<float>(index++);
        if (!p.open || p.broken) continue;
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        int pushedVars = 0;
        if (p.window) {
            // A small window of its own: it opens where the menu item was clicked, cannot be docked, fits its content.
            flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
            if (p.placeAtMouse) {
                ImGui::SetNextWindowPos(p.anchor, ImGuiCond_Always);
                p.placeAtMouse = false;
            } else {
                ImGui::SetNextWindowPos(ImVec2(520.0f, 140.0f), ImGuiCond_FirstUseEver); // a window opened at start: over the viewport
            }
            ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID); // inside the editor window, not a window of the OS
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
            pushedVars = 2;
        } else {
            // Plugin panels float until the user docks them; stagger the first positions so they do not overlap.
            ImGui::SetNextWindowPos(ImVec2(260.0f + cascade, 160.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(300.0f, 220.0f), ImGuiCond_FirstUseEver);
        }
        const bool visible = ImGui::Begin(p.title.c_str(), &p.open, flags);
        if (pushedVars) ImGui::PopStyleVar(pushedVars);
        if (visible) {
            const float width = p.window ? (p.width > 0.0f ? p.width : 380.0f) : 0.0f;
            sol::state_view lua(*p.plugin->lua);
            // The widget table is rebuilt per call so it always belongs to the plugin's own state.
            sol::table ui = lua.create_table();
            ui["text"] = [](const std::string& s) { ImGui::TextUnformatted(s.c_str()); };
            ui["dim"] = [](const std::string& s) { ImGui::TextDisabled("%s", s.c_str()); };
            ui["heading"] = [](const std::string& s) {
                ImGui::SetWindowFontScale(1.3f);
                ImGui::TextUnformatted(s.c_str());
                ImGui::SetWindowFontScale(1.0f);
            };
            ui["wrapped"] = [width](const std::string& s) {
                ImGui::PushTextWrapPos(width > 0.0f ? ImGui::GetCursorPosX() + width : 0.0f);
                ImGui::TextUnformatted(s.c_str());
                ImGui::PopTextWrapPos();
            };
            ui["colored"] = [](float r, float g, float b, const std::string& s) { ImGui::TextColored(ImVec4(r, g, b, 1.0f), "%s", s.c_str()); };
            ui["dot"] = [](float r, float g, float b) { // a status light, then the next item on the same line
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float h = ImGui::GetTextLineHeight();
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + 5.0f, pos.y + h * 0.5f), 5.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f)));
                ImGui::Dummy(ImVec2(12.0f, h));
                ImGui::SameLine();
            };
            ui["space"] = [](float h) { ImGui::Dummy(ImVec2(1.0f, h)); };
            ui["separator"] = [] { ImGui::Separator(); };
            ui["same_line"] = [] { ImGui::SameLine(); };
            ui["button"] = [width](const std::string& label, sol::optional<float> w) {
                float wanted = w.value_or(0.0f);
                if (wanted < 0.0f) wanted = width; // -1: as wide as the window's content
                return ImGui::Button(label.c_str(), ImVec2(wanted, 0.0f));
            };
            ui["checkbox"] = [](const std::string& label, bool value) {
                ImGui::Checkbox(label.c_str(), &value);
                return value;
            };
            ui["slider"] = [](const std::string& label, float value, float lo, float hi) {
                ImGui::SliderFloat(label.c_str(), &value, lo, hi);
                return value;
            };
            ui["int_slider"] = [](const std::string& label, int value, int lo, int hi) {
                ImGui::SliderInt(label.c_str(), &value, lo, hi);
                return value;
            };
            ui["input"] = [](const std::string& label, const std::string& value) {
                char buffer[256] = {};
                std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
                ImGui::InputText(label.c_str(), buffer, sizeof(buffer));
                return std::string(buffer);
            };
            if (width > 0.0f) ImGui::Dummy(ImVec2(width, 0.0f)); // fixes the width of a window that fits its content
            m_impl->Call(p, "panel", ui);
        }
        ImGui::End();
    }
}

void PluginHost::DrawPluginsMenu() {
    if (!ImGui::BeginMenu("Plugins")) return;
    // One entry per plugin: a plugin with a window opens it when clicked; one without is only listed.
    for (const Info& info : Plugins()) {
        if (!info.error.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.3f, 1.0f), "%s (failed, see Console)", info.name.c_str());
            continue;
        }
        int own = 0;
        for (const PanelEntry& p : m_impl->panels) own += p.plugin->name == info.name;
        if (own == 0) {
            ImGui::TextDisabled("%s", info.name.c_str());
            continue;
        }
        for (PanelEntry& p : m_impl->panels) {
            if (p.plugin->name != info.name) continue;
            const std::string label = own == 1 ? info.name : info.name + " / " + p.title;
            const bool wasOpen = p.open;
            ImGui::MenuItem(label.c_str(), nullptr, &p.open, !p.broken);
            if (!wasOpen && p.open) { // it was just opened: windows appear where the mouse is
                p.anchor = ImGui::GetMousePos();
                p.placeAtMouse = true;
            }
        }
    }
    if (m_impl->plugins.empty()) ImGui::TextDisabled("No plugins in Plugins/");
    ImGui::Separator();
    if (ImGui::MenuItem("Reload Plugins")) m_impl->reloadRequested = true;
    ImGui::EndMenu();
}

bool PluginHost::OpenPanel(const std::string& name, float x, float y) {
    for (PanelEntry& p : m_impl->panels) {
        if (p.broken || (p.plugin->name != name && p.title != name)) continue;
        p.open = true;
        p.anchor = ImVec2(x, y);
        p.placeAtMouse = true;
        return true;
    }
    return false;
}

void PluginHost::Update(float deltaTime) {
    if (m_impl->reloadRequested) {
        m_impl->reloadRequested = false;
        Reload();
    }
    for (Callback& cb : m_impl->updates) m_impl->Call(cb, "on_update", deltaTime);
}

bool PluginHost::RunCommand(const std::string& name, const std::string& args) {
    const auto it = m_impl->commands.find(name);
    if (it == m_impl->commands.end()) return false;
    m_impl->Call(it->second, "command", args);
    return true;
}

std::vector<PluginHost::Info> PluginHost::Plugins() const {
    std::vector<Info> out;
    for (const auto& p : m_impl->plugins) out.push_back({p->name, p->error, p->menus, p->commands, p->panels});
    return out;
}

} // namespace magnum::editor
