// MagnumMcp: the Model Context Protocol server of the Magnum engine. An AI client starts it (stdio), and through it can build and
// run whole games: it starts or attaches to the Magnum editor, edits scenes, actors and properties, writes scripts, shaders, UI
// documents and materials, plays the game, presses keys, takes screenshots and reads the log.
//
//   MagnumMcp [--project <folder>] [--no-launch]
//
// Messages are JSON-RPC 2.0, one per line, on stdin and stdout (nothing else is ever printed to stdout; diagnostics go to stderr).
#include "EditorLink.h"
#include "Tools.h"

#include <fcntl.h>
#include <io.h>
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace magnum::mcp;
namespace fs = std::filesystem;

namespace {

constexpr const char* kServerName = "magnum-engine";
constexpr const char* kServerVersion = "0.1.0";
const char* const kProtocols[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

void Log(const std::string& text) {
    std::fprintf(stderr, "[magnum-mcp] %s\n", text.c_str());
    std::fflush(stderr);
}

void Send(const Json& message) {
    const std::string text = message.dump(-1, ' ', false, Json::error_handler_t::replace);
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

Json ErrorResponse(const Json& id, int code, const std::string& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

Json OkResponse(const Json& id, Json result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

fs::path ExeDir() {
    wchar_t buffer[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
    return fs::path(std::wstring(buffer, n)).parent_path();
}

bool ReadAll(const fs::path& file, std::string& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream text;
    text << in.rdbuf();
    out = text.str();
    return true;
}

const char* const kGuide = R"(# Working with the Magnum engine

You control a game engine through this server. The engine has an editor (a window), a scene of actors with components, Lua scripts, shaders, a UI system and a 2D/3D renderer.

## Loop
1. `editor_status` - what is open. The editor starts by itself on the first call.
2. Read before writing: `docs_read` (UI.md, 2D.md), `reference_components` (all components and their properties), `project_files`, `project_read`.
3. Build: `scene_spawn` / `scene_set` for actors, `project_write` for scripts (Content/Scripts), UI documents (Content/UI), shaders, materials, pipelines.
   Lua files hot-reload; the answer of `project_write` contains the errors the editor logged. Fix them and write again.
4. Look: `viewport_screenshot` after every visible change.
5. Test: `play_start`, `play_keys`, `play_mouse`, `play_wait`, `play_lua`, `viewport_screenshot`, `log_read`, `play_stop`. Play never changes the saved scene.
6. Keep: `scene_save`.

## Facts
- Positions are in world units; Y is up. The editor camera looks along yaw/pitch (yaw -90 looks down -Z).
- Actors are found by name or by id. Names are unique.
- A game is: actors with Script components (Content/Scripts/*.lua), a camera actor, optional Canvas actors with UI documents (Content/UI/*.lua).
- 2D games: sprites (Place Actors > 2D), an orthographic camera (`camera2d`), Rigidbody with Lock Z. See docs_read 2D.md.
- UI is written in Lua (ui.panel{...}, ui.button{...}); see docs_read UI.md.
- Mistakes are reported: tool results with isError set carry the editor's warnings and errors.
)";

} // namespace

int main(int argc, char** argv) {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    const fs::path exeDir = ExeDir();
    fs::path engineRoot = fs::path(MAGNUM_ENGINE_DIR);
    fs::path project = engineRoot / "Projects" / "MagnumDemo";
    if (const char* env = std::getenv("MAGNUM_PROJECT"); env && *env) project = env;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project" && i + 1 < argc) project = argv[++i];
        else if (arg == "--engine" && i + 1 < argc) engineRoot = argv[++i];
    }
    std::error_code ec;
    project = fs::weakly_canonical(fs::absolute(project), ec);

    EditorLink link(exeDir, engineRoot, project);
    ToolSet tools(link);
    Log("started; project " + project.generic_string());

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        Json message = Json::parse(line, nullptr, false);
        if (message.is_discarded() || !message.is_object()) {
            Send(ErrorResponse(nullptr, -32700, "parse error"));
            continue;
        }
        const std::string method = message.value("method", std::string());
        const bool isRequest = message.contains("id") && !method.empty();
        const Json id = message.contains("id") ? message["id"] : Json(nullptr);
        const Json params = message.value("params", Json::object());
        if (method.empty()) continue; // a response to something we never asked

        if (method == "initialize") {
            std::string version = kProtocols[0];
            const std::string wanted = params.value("protocolVersion", std::string());
            for (const char* p : kProtocols) {
                if (wanted == p) version = p;
            }
            Send(OkResponse(id, {{"protocolVersion", version},
                                 {"capabilities", {{"tools", {{"listChanged", false}}}, {"resources", {{"subscribe", false}, {"listChanged", false}}}, {"prompts", {{"listChanged", false}}}}},
                                 {"serverInfo", {{"name", kServerName}, {"title", "Magnum Engine"}, {"version", kServerVersion}}},
                                 {"instructions", "Controls the Magnum game engine: scenes, actors, scripts, shaders, UI, playing and screenshots. Start with editor_status, read the docs with docs_read, check results with viewport_screenshot."}}));
        } else if (method == "notifications/initialized" || method == "notifications/cancelled" || method.rfind("notifications/", 0) == 0) {
            // nothing to do
        } else if (method == "ping") {
            if (isRequest) Send(OkResponse(id, Json::object()));
        } else if (method == "tools/list") {
            Send(OkResponse(id, tools.ListJson()));
        } else if (method == "tools/call") {
            const std::string name = params.value("name", std::string());
            if (!tools.Find(name)) {
                Send(ErrorResponse(id, -32602, "unknown tool '" + name + "'"));
                continue;
            }
            Log("tool " + name);
            const ToolResult r = tools.Call(name, params.value("arguments", Json::object()));
            Send(OkResponse(id, {{"content", r.content}, {"isError", r.isError}}));
        } else if (method == "resources/list") {
            Json resources = Json::array();
            resources.push_back({{"uri", "magnum://guide"}, {"name", "guide"}, {"title", "How to work with the engine"}, {"mimeType", "text/markdown"}});
            resources.push_back({{"uri", "magnum://reference/components"}, {"name", "components"}, {"title", "Components, properties and spawn kinds"}, {"mimeType", "text/markdown"}});
            resources.push_back({{"uri", "magnum://reference/console"}, {"name", "console"}, {"title", "Editor console commands"}, {"mimeType", "text/markdown"}});
            for (const auto& [docName, path] : tools.Docs()) {
                resources.push_back({{"uri", "magnum://docs/" + docName}, {"name", docName}, {"title", docName}, {"mimeType", "text/markdown"}});
            }
            Send(OkResponse(id, {{"resources", resources}}));
        } else if (method == "resources/templates/list") {
            Send(OkResponse(id, {{"resourceTemplates", Json::array()}}));
        } else if (method == "resources/read") {
            const std::string uri = params.value("uri", std::string());
            std::string text;
            bool found = true;
            if (uri == "magnum://guide") text = kGuide;
            else if (uri == "magnum://reference/components") text = tools.ComponentReference();
            else if (uri == "magnum://reference/console") text = tools.ConsoleReference();
            else if (uri.rfind("magnum://docs/", 0) == 0) {
                found = false;
                for (const auto& [docName, path] : tools.Docs()) {
                    if (uri == "magnum://docs/" + docName) found = ReadAll(path, text);
                }
            } else {
                found = false;
            }
            if (!found) Send(ErrorResponse(id, -32002, "resource not found: " + uri));
            else Send(OkResponse(id, {{"contents", Json::array({{{"uri", uri}, {"mimeType", "text/markdown"}, {"text", text}}})}}));
        } else if (method == "prompts/list") {
            Json prompts = Json::array();
            prompts.push_back({{"name", "make_a_game"}, {"title", "Make a game"}, {"description", "Plan and build a small game in the engine from a description."},
                               {"arguments", Json::array({{{"name", "idea"}, {"description", "What the game is."}, {"required", true}}})}});
            prompts.push_back({{"name", "make_a_ui"}, {"title", "Make a UI screen"}, {"description", "Write a UI document (menu, HUD, dialog) and check it on screen."},
                               {"arguments", Json::array({{{"name", "what"}, {"description", "What the screen shows and does."}, {"required", true}}})}});
            prompts.push_back({{"name", "debug_the_game"}, {"title", "Find and fix a problem"}, {"description", "Play the game, find what is wrong and fix it."},
                               {"arguments", Json::array({{{"name", "problem"}, {"description", "What goes wrong."}, {"required", false}}})}});
            Send(OkResponse(id, {{"prompts", prompts}}));
        } else if (method == "prompts/get") {
            const std::string name = params.value("name", std::string());
            const Json args = params.value("arguments", Json::object());
            std::string text;
            if (name == "make_a_game") {
                text = std::string("Build this game in the Magnum engine: ") + args.value("idea", std::string("(no description)")) +
                       "\n\nRead the guide resource (magnum://guide) and docs_read 2D.md or UI.md as needed. Work in small steps: create actors, write the scripts and UI documents, "
                       "take a viewport_screenshot after each visible change, then play_start and test with play_keys / play_mouse, read log_read for errors. Save the scene at the end.";
            } else if (name == "make_a_ui") {
                text = std::string("Make this UI screen: ") + args.value("what", std::string("(no description)")) +
                       "\n\nRead docs_read UI.md first. Write a document in Content/UI/ with project_write, add a Canvas actor (scene_spawn kind canvas, Document = the file) if there is none, "
                       "then look at it with viewport_screenshot and improve it until it looks right. Test clicks with play_start and play_mouse.";
            } else if (name == "debug_the_game") {
                text = std::string("Find and fix a problem in the game. ") + args.value("problem", std::string("Nothing specific is known: play it and look.")) +
                       "\n\nplay_start, exercise it with play_keys / play_mouse / play_wait, check viewport_screenshot and log_read, use play_lua to inspect state, "
                       "then fix the scripts with project_edit (they hot-reload) and test again.";
            } else {
                Send(ErrorResponse(id, -32602, "unknown prompt '" + name + "'"));
                continue;
            }
            Send(OkResponse(id, {{"description", name}, {"messages", Json::array({{{"role", "user"}, {"content", {{"type", "text"}, {"text", text}}}}})}}));
        } else if (isRequest) {
            Send(ErrorResponse(id, -32601, "method not found: " + method));
        }
    }
    Log("stdin closed; leaving (the editor keeps running)");
    return 0;
}
