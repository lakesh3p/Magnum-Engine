#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace magnum::editor {

// What a plugin may ask of the editor. Everything goes through the same console commands a user can type,
// so plugin edits are undoable and cannot do more than the console can.
// The state of the AI control channel (what `magnum.mcp()` gives a plugin).
struct McpInfo {
    bool running = false;
    int port = 0;
    bool client = false;       // an AI client is connected
    bool serverBuilt = false;  // MagnumMcp.exe exists next to the editor
    std::string serverPath;
};

struct PluginServices {
    std::function<void(const std::string&)> exec;           // run a console command line
    std::function<std::string()> selectedName;              // name of the selected actor, "" = none
    std::function<std::vector<std::string>()> actorNames;   // every actor, in outliner order
    std::function<std::string()> mcpStatus;                 // the state of the AI control channel, as text
    std::function<McpInfo()> mcpInfo;                       // ... and as values
};

// Loads Plugins/<name>/plugin.lua. Each plugin runs in its own sandboxed Lua state (no io, os or require) and
// registers menu items, console commands, panels and per-frame callbacks through the `magnum` table:
//
//   magnum.log(text) / magnum.warn(text) / magnum.error(text)
//   magnum.exec(command)               run a console command, e.g. magnum.exec("spawn cube")
//   magnum.selected()                  name of the selected actor, or nil
//   magnum.actors()                    list of actor names
//   magnum.mcp_status()                text about the control channel that AI clients (MagnumMcp) use
//   magnum.mcp()                       the same as a table: running, port, client, server_built, server_path
//   magnum.panel("Title", fn(ui), {window = true, width = 420, open = true})   a window that opens from Tools > Plugins at the mouse and
//                                    cannot be docked (open = true: shown from the start); ui also has heading, wrapped, colored(r, g, b, text), dot(r, g, b), space(h)
//   magnum.menu("Tools/My Action", fn) add a menu item (the part before the first / is the menu)
//   magnum.command("name", fn(args))   add a console command
//   magnum.panel("Title", fn(ui))      add a panel; ui.text, ui.button(label[, width]), ui.checkbox, ui.slider, ui.input, ...
//   magnum.on_update(fn(dt))           called every frame
//
// A callback that raises an error is reported once and switched off until the plugins are reloaded.
class PluginHost {
public:
    PluginHost();
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    void Init(const std::filesystem::path& pluginsDir, PluginServices services);
    void Reload(); // forgets everything and loads every plugin again

    // Menu items registered under `menu`; call between BeginMenu/EndMenu. Returns true if anything was drawn.
    bool DrawMenuItems(const std::string& menu);
    // Names of menus that plugins created and the editor does not have itself.
    std::vector<std::string> ExtraMenus(const std::vector<std::string>& builtIn) const;
    void DrawPanels();
    // The Tools > Plugins submenu: panel toggles, a list of loaded plugins and Reload. Reloading is deferred to Update().
    void DrawPluginsMenu();
    void Update(float deltaTime);
    // Opens the window of a plugin (by plugin or panel name) at a screen position. False when there is none.
    bool OpenPanel(const std::string& name, float x, float y);
    // Runs a plugin-defined console command. False if there is none with that name.
    bool RunCommand(const std::string& name, const std::string& args);

    struct Info {
        std::string name;
        std::string error; // empty = loaded
        int menus = 0, commands = 0, panels = 0;
    };
    std::vector<Info> Plugins() const;
    // Loads one plugin source directly (used by tests); returns the error text or "".
    std::string LoadSource(const std::string& name, const std::string& source);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace magnum::editor
