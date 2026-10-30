#include "Tools.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace magnum::mcp {

namespace fs = std::filesystem;

// ---- small helpers --------------------------------------------------------------------------------------------------------------

std::string Base64Encode(const std::string& bytes) {
    static const char* const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t a = static_cast<unsigned char>(bytes[i]);
        const uint32_t b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        const uint32_t c = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        const uint32_t n = (a << 16) | (b << 8) | c;
        out += alphabet[(n >> 18) & 63];
        out += alphabet[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[n & 63] : '=';
    }
    return out;
}

static std::string Base64Decode(const std::string& text, bool& ok) {
    std::string out;
    uint32_t buffer = 0;
    int bits = 0;
    ok = true;
    for (const char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=' || std::isspace(static_cast<unsigned char>(c))) continue;
        else {
            ok = false;
            return {};
        }
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xFF);
        }
    }
    return out;
}

ToolResult TextResult(const std::string& text, bool isError) {
    ToolResult r;
    r.content.push_back({{"type", "text"}, {"text", text}});
    r.isError = isError;
    return r;
}

ToolResult ImageResult(const std::string& base64Png, const std::string& caption) {
    ToolResult r;
    r.content.push_back({{"type", "image"}, {"data", base64Png}, {"mimeType", "image/png"}});
    if (!caption.empty()) r.content.push_back({{"type", "text"}, {"text", caption}});
    return r;
}

namespace {

ToolResult Fail(const std::string& message) { return TextResult(message, true); }

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// ---- schema builders ----------------------------------------------------------------------------------------------------------

Json Typed(const char* type, const std::string& description) { return {{"type", type}, {"description", description}}; }
Json Str(const std::string& d) { return Typed("string", d); }
Json Num(const std::string& d) { return Typed("number", d); }
Json Int(const std::string& d) { return Typed("integer", d); }
Json Bool(const std::string& d) { return Typed("boolean", d); }
Json AnyValue(const std::string& d) { return {{"description", d}}; }
Json Enum(std::vector<std::string> values, const std::string& d) {
    Json e = Typed("string", d);
    e["enum"] = std::move(values);
    return e;
}
Json Vec(int n, const std::string& d) {
    Json v = Typed("array", d);
    v["items"] = {{"type", "number"}};
    v["minItems"] = n;
    v["maxItems"] = n;
    return v;
}
Json ArrayOf(Json items, const std::string& d) {
    Json v = Typed("array", d);
    v["items"] = std::move(items);
    return v;
}
Json Object(const std::string& d) {
    Json o = Typed("object", d);
    o["additionalProperties"] = true;
    return o;
}
Json Schema(Json properties = Json::object(), std::vector<std::string> required = {}) {
    Json s = {{"type", "object"}, {"properties", std::move(properties)}, {"additionalProperties", false}};
    if (!required.empty()) s["required"] = std::move(required);
    return s;
}

std::string Pretty(const Json& j) { return j.dump(2, ' ', false, Json::error_handler_t::replace); }

// ---- files of the project ---------------------------------------------------------------------------------------------------------

struct ResolvedPath {
    fs::path full;
    fs::path relative;
    bool engine = false; // inside the engine folder (Engine/...), read only
};

// A project-relative path ("Content/Scripts/a.lua", or "Engine/..." for the engine's own files). Absolute paths and ".." are refused.
bool ResolvePath(EditorLink& link, const std::string& text, bool forWrite, ResolvedPath& out, std::string& error) {
    if (text.empty()) {
        error = "the path is empty";
        return false;
    }
    fs::path rel = fs::path(text).lexically_normal();
    if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) {
        error = "use a path relative to the project, like Content/Scripts/player.lua";
        return false;
    }
    for (const fs::path& part : rel) {
        if (part == "..") {
            error = "the path leaves the project";
            return false;
        }
    }
    const std::string first = rel.empty() ? std::string() : rel.begin()->string();
    out.relative = rel;
    if (first == "Engine") {
        out.engine = true;
        out.full = link.EngineRoot() / rel;
        if (forWrite) {
            error = "the engine's own files (Engine/...) are read only";
            return false;
        }
        return true;
    }
    out.full = link.Project() / rel;
    if (forWrite) {
        const std::string ext = Lower(rel.extension().string());
        if (first == "Saved" || first == "build" || first == ".git" || ext == ".magnum") {
            error = "files in Saved/, build folders and the .magnum project file are managed by the editor";
            return false;
        }
    }
    return true;
}

bool ReadFileText(const fs::path& file, std::string& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream text;
    text << in.rdbuf();
    out = text.str();
    return true;
}

bool IsImagePath(const fs::path& p) {
    const std::string ext = Lower(p.extension().string());
    return ext == ".png";
}

bool LooksBinary(const std::string& bytes) {
    const size_t n = std::min<size_t>(bytes.size(), 4096);
    for (size_t i = 0; i < n; ++i) {
        if (bytes[i] == '\0') return true;
    }
    return false;
}

// ---- running a program and capturing what it prints -----------------------------------------------------------------------------------

bool RunProgram(const fs::path& exe, const std::string& arguments, const fs::path& cwd, int timeoutSeconds, std::string& output, int& exitCode, std::string& error) {
    const fs::path capture = fs::temp_directory_path() / ("magnum_mcp_run_" + std::to_string(GetCurrentProcessId()) + ".txt");
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE file = CreateFileW(capture.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "cannot create a temporary file for the output";
        return false;
    }
    std::wstring command = L"\"" + exe.wstring() + L"\" " + fs::path(arguments).wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = file;
    startup.hStdError = file;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    // Inherit only through the explicit handles above; the MCP pipes stay with this process.
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd.wstring().c_str(), &startup, &process)) {
        error = "could not start " + exe.string() + " (error " + std::to_string(GetLastError()) + ")";
        CloseHandle(file);
        return false;
    }
    const DWORD waited = WaitForSingleObject(process.hProcess, static_cast<DWORD>(timeoutSeconds) * 1000);
    if (waited == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        error = "timed out after " + std::to_string(timeoutSeconds) + " seconds";
    }
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    exitCode = static_cast<int>(code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    CloseHandle(file);
    ReadFileText(capture, output);
    std::error_code ec;
    fs::remove(capture, ec);
    return waited != WAIT_TIMEOUT;
}

// The last `lines` lines of a text.
std::string Tail(const std::string& text, size_t lines) {
    size_t count = 0, pos = text.size();
    while (pos > 0 && count <= lines) {
        pos = text.rfind('\n', pos - 1);
        if (pos == std::string::npos) return text;
        ++count;
    }
    return count > lines ? text.substr(pos + 1) : text;
}

// ---- templates for new assets ------------------------------------------------------------------------------------------------------

const char* const kScriptTemplate = R"(-- A gameplay script. Put it on an actor (Script component). Everything is optional; `self` is the actor.
--   self.position / .rotation / .scale / .velocity   vec3      self.grounded   standing on something
--   input.key("w")  input.pressed("space")  input.mouse_delta()      world.find("Name")  world.spawn("cube", x, y, z)
--   hud.text(0.02, 0.03, "hello")    ui.find("Score").text = "..."    game.stop()    log("anything")

function on_start(self)
    self.state.t = 0
end

function on_update(self, dt)
    self.state.t = self.state.t + dt
    self.rotation = vec3(0, self.state.t * 45, 0)
end

-- function on_hit(self, other) end
-- function on_trigger(self, other) end
-- function on_trigger_exit(self, other) end
)";

const char* const kShaderTemplate = R"(-- A surface shader. Save the file and the viewport updates.
local S = shader("New Shader")

local position  = S:attribute("position", "vec3")
local normal    = S:attribute("normal", "vec3")

local model         = S:builtin("model")
local view_proj     = S:builtin("view_proj")
local normal_matrix = S:builtin("normal_matrix")
local light_dir     = S:builtin("light_dir")
local light_color   = S:builtin("light_color")

local base_color = S:param("base_color", "color", { 0.8, 0.3, 0.2 })

S:vertex(function(a)
    return view_proj * (model * vec4(a.position, 1.0)), {
        normal = normalize(normal_matrix * a.normal),
    }
end)

S:fragment(function(v)
    local ndl = max(dot(normalize(v.normal), light_dir), 0.0)
    local lit = base_color * (0.15 + light_color * ndl)
    return vec4(pow(lit, vec3(1.0 / 2.2)), 1.0)
end)

return S
)";

const char* const kUiTemplate = R"(-- A UI document: it returns the root node of a screen. Put it on a Canvas actor and save to see it live.
return ui.panel{ name = "Screen", anchor = "fill",
    ui.column{ anchor = "center", gap = 12, padding = 24, items = "center",
        bg = "#10131dd0", radius = 10, border = { color = "#ffffff18", width = 1 },
        ui.text{ "Hello", font = "SansBold", font_size = 36 },
        ui.button{ "Click me", size = { 200, 44 }, on_click = function(button) button.text = "Clicked!" end },
    },
}
)";

} // namespace

// ---- the tool set ----------------------------------------------------------------------------------------------------------------------

ToolSet::ToolSet(EditorLink& link) : m_link(link) {
    Register();
}

const Tool* ToolSet::Find(const std::string& name) const {
    for (const Tool& t : m_tools) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

ToolResult ToolSet::Call(const std::string& name, const Json& arguments) const {
    const Tool* tool = Find(name);
    if (!tool) return Fail("unknown tool '" + name + "'");
    try {
        return tool->run(arguments.is_object() ? arguments : Json::object());
    } catch (const Json::exception& e) {
        return Fail(std::string("invalid arguments: ") + e.what());
    } catch (const std::exception& e) {
        return Fail(std::string("the tool failed: ") + e.what());
    }
}

Json ToolSet::ListJson() const {
    Json tools = Json::array();
    for (const Tool& t : m_tools) {
        Json annotations = {{"title", t.title}, {"readOnlyHint", t.readOnly}, {"openWorldHint", false}};
        if (!t.readOnly) annotations["destructiveHint"] = t.destructive;
        tools.push_back({{"name", t.name}, {"title", t.title}, {"description", t.description}, {"inputSchema", t.inputSchema}, {"annotations", annotations}});
    }
    return {{"tools", tools}};
}

std::vector<std::pair<std::string, fs::path>> ToolSet::Docs() const {
    std::vector<std::pair<std::string, fs::path>> docs;
    std::error_code ec;
    const fs::path dir = m_link.EngineRoot() / "docs";
    if (fs::is_directory(dir, ec)) {
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() == ".md") docs.emplace_back(entry.path().filename().string(), entry.path());
        }
    }
    std::sort(docs.begin(), docs.end());
    if (fs::exists(m_link.EngineRoot() / "CHANGELOG.md", ec)) docs.emplace_back("CHANGELOG.md", m_link.EngineRoot() / "CHANGELOG.md");
    return docs;
}

std::string ToolSet::ComponentReference() {
    Json r;
    std::string error;
    if (!m_link.Request("reflect", Json::object(), r, error)) return "The editor is not available: " + error;
    std::ostringstream out;
    out << "# Components\n\nSet properties with scene_set / scene_spawn: {\"Component\": {\"Property\": value}}. Vectors and colors are [x, y, z] (colors 0..1 or \"#rrggbb\"), enums are their names.\n\n";
    for (const Json& c : r["components"]) {
        out << "## " << c["name"].get<std::string>() << (c.value("removable", true) ? "" : "  (fixed)") << (c.value("from_game_module", false) ? "  (game module)" : "") << "\n";
        for (const Json& p : c["properties"]) {
            out << "- " << p["name"].get<std::string>() << ": " << p["kind"].get<std::string>();
            if (p.contains("options")) {
                out << " (";
                bool first = true;
                for (const Json& o : p["options"]) {
                    out << (first ? "" : ", ") << o.get<std::string>();
                    first = false;
                }
                out << ")";
            }
            if (p.contains("range")) out << " [" << p["range"][0].dump() << " .. " << p["range"][1].dump() << "]";
            out << "\n";
        }
        out << "\n";
    }
    out << "# Spawn kinds\n";
    for (const Json& k : r["spawn_kinds"]) out << "- " << k["kind"].get<std::string>() << "  (" << k["name"].get<std::string>() << ")\n";
    return out.str();
}

std::string ToolSet::ConsoleReference() {
    Json r;
    std::string error;
    if (!m_link.Request("exec", {{"line", "help"}}, r, error)) return "The editor is not available: " + error;
    return "# Editor console\n\nThe console_run tool sends these commands to the editor.\n\n" + r.value("text", std::string());
}

void ToolSet::Register() {
    EditorLink& link = m_link;
    auto add = [this](const char* name, const char* title, const char* description, Json schema, bool readOnly, bool destructive, std::function<ToolResult(const Json&)> run) {
        Tool t;
        t.name = name;
        t.title = title;
        t.description = description;
        t.inputSchema = std::move(schema);
        t.readOnly = readOnly;
        t.destructive = destructive;
        t.run = std::move(run);
        m_tools.push_back(std::move(t));
    };

    // Asks the editor something; returns false with the failure as a ready tool result.
    auto ask = [&link](const std::string& op, Json args, Json& response, ToolResult& failure, int timeout = 60) {
        std::string error;
        if (link.Request(op, std::move(args), response, error, timeout)) return true;
        failure = Fail(error);
        return false;
    };
    // Runs a console line and reports what it printed.
    auto exec = [ask](const std::string& line, int timeout = 60) -> ToolResult {
        Json r;
        ToolResult failure;
        if (!ask("exec", {{"line", line}}, r, failure, timeout)) return failure;
        bool problems = false;
        for (const Json& e : r["output"]) problems = problems || e.value("level", "info") != "info";
        const std::string text = r.value("text", std::string());
        return TextResult(text.empty() ? "(done)" : text, problems);
    };
    // The log lines that appeared in the editor after `mark`.
    auto logSince = [ask](uint64_t mark, bool onlyProblems) {
        Json r;
        ToolResult ignored;
        std::string text;
        if (!ask("log", {{"since", mark}, {"max", 200}}, r, ignored)) return text;
        for (const Json& e : r["entries"]) {
            const std::string level = e.value("level", "info");
            if (onlyProblems && level == "info") continue;
            text += (text.empty() ? "" : "\n") + std::string(level == "info" ? "" : (level == "warn" ? "warning: " : "error: ")) + e.value("text", std::string());
        }
        return text;
    };
    auto actorArgs = [](const Json& a) {
        Json out = Json::object();
        if (a.contains("actor")) out["actor"] = a["actor"];
        return out;
    };

    const Json actorProp = Str("The actor: its name, or its id (16 hex digits, as returned by scene_list).");

    // ---- the editor itself ----------------------------------------------------------------------------------------------------------

    add("editor_status", "Editor status", "State of the Magnum editor: project, open scene, unsaved changes, playing or not, selection, camera, UI canvases, viewport rectangle. Starts the editor when it is not running.",
        Schema(), true, false, [=](const Json&) {
            Json r;
            ToolResult failure;
            if (!ask("status", Json::object(), r, failure)) return failure;
            r.erase("id");
            r.erase("ok");
            return TextResult(Pretty(r));
        });

    add("editor_launch", "Open a project in the editor",
        "Starts the Magnum editor on a project folder (or attaches to one that is already open on it). Use it to work on another project; an editor open on a different project is closed first. A folder that does not exist or is empty becomes a new project made from the template.",
        Schema({{"project", Str("Absolute path of the project folder (the one with the .magnum file). Default: the project the server was started with.")},
                {"discard_changes", Bool("Close an editor that has unsaved changes anyway. Default false.")}}),
        false, false, [&link](const Json& a) {
            fs::path project = a.contains("project") ? fs::path(a["project"].get<std::string>()) : link.Project();
            std::string error;
            if (!link.Launch(project, a.value("discard_changes", false), error)) return Fail(error);
            return TextResult("The editor is open on " + link.Project().generic_string());
        });

    add("editor_quit", "Close the editor", "Closes the editor window. Refuses when there are unsaved changes unless discard_changes is true.",
        Schema({{"discard_changes", Bool("Close even with unsaved changes. Default false.")}}), false, true, [&link](const Json& a) {
            if (!link.AttachIfRunning()) return TextResult("The editor is not running.");
            std::string error;
            if (!link.Quit(a.value("discard_changes", false), error)) return Fail(error);
            return TextResult("The editor was closed.");
        });

    add("console_run", "Run an editor console command",
        "Runs one command of the editor console and returns what it printed (spawn, select, set, get, undo, save, open, play, stop, graph, ui, lua ...). Most things also have a dedicated tool; use console_help for the full list.",
        Schema({{"command", Str("One console line, for example: spawn cube")}}, {"command"}), false, false, [=](const Json& a) { return exec(a.at("command").get<std::string>()); });

    add("console_help", "Console command list", "Lists every command of the editor console.", Schema(), true, false, [=](const Json&) { return exec("help"); });

    add("log_read", "Read the editor log", "Recent lines of the editor log (shader compile errors, script errors, command output).",
        Schema({{"max", Int("How many lines, at most (default 100).")}}), true, false, [=](const Json& a) {
            Json r;
            ToolResult failure;
            if (!ask("log", {{"max", a.value("max", 100)}}, r, failure)) return failure;
            std::string text;
            for (const Json& e : r["entries"]) {
                const std::string level = e.value("level", "info");
                text += std::string(level == "info" ? "" : (level == "warn" ? "warning: " : "error: ")) + e.value("text", std::string()) + "\n";
            }
            return TextResult(text.empty() ? "(the log is empty)" : text);
        });

    // ---- scene ---------------------------------------------------------------------------------------------------------------------------

    add("scene_list", "List actors", "The actors of the open scene: id, name, components and position. Filter by a part of the name or of a component name.",
        Schema({{"filter", Str("Only actors whose name or a component name contains this text.")}, {"limit", Int("At most this many (default 500).")}}), true, false, [=](const Json& a) {
            Json r;
            ToolResult failure;
            Json args = Json::object();
            if (a.contains("filter")) args["filter"] = a["filter"];
            if (a.contains("limit")) args["limit"] = a["limit"];
            if (!ask("scene", args, r, failure)) return failure;
            std::ostringstream out;
            out << "scene: " << (r["scene"].get<std::string>().empty() ? "(untitled)" : r["scene"].get<std::string>()) << (r.value("dirty", false) ? "  (unsaved changes)" : "")
                << "\n" << r["matching"].get<int>() << " actor(s)" << (r["returned"] != r["matching"] ? ", showing " + std::to_string(r["returned"].get<int>()) : "") << "\n";
            for (const Json& actor : r["actors"]) {
                out << actor["id"].get<std::string>() << "  " << actor["name"].get<std::string>() << "  [";
                bool first = true;
                for (const Json& c : actor["components"]) {
                    if (c == "Transform") continue;
                    out << (first ? "" : ", ") << c.get<std::string>();
                    first = false;
                }
                out << "]";
                if (actor.contains("position")) out << "  @ " << actor["position"].dump();
                out << "\n";
            }
            return TextResult(out.str());
        });

    add("scene_get_actor", "Get an actor", "Every component and property value of one actor.", Schema({{"actor", actorProp}}, {"actor"}), true, false, [=](const Json& a) {
        Json r;
        ToolResult failure;
        if (!ask("actor", actorArgs(a), r, failure)) return failure;
        return TextResult(Pretty(r["actor"]));
    });

    add("scene_dump", "Dump the scene as JSON", "The whole open scene in the .scene file format (all actors, all property values). Large for big scenes: prefer scene_list and scene_get_actor.",
        Schema(), true, false, [=](const Json&) {
            Json r;
            ToolResult failure;
            if (!ask("scene_file", Json::object(), r, failure)) return failure;
            return TextResult(r["scene"].dump(1, ' ', false, Json::error_handler_t::replace));
        });

    add("reference_components", "Component and spawn kind reference", "Every component with its properties (kind, enum options, ranges) and every spawn kind. Read it before setting properties.",
        Schema(), true, false, [this](const Json&) {
            const std::string text = ComponentReference();
            return TextResult(text, text.rfind("The editor is not available", 0) == 0);
        });

    add("scene_spawn", "Create an actor",
        "Creates an actor of a kind (cube, sphere, plane, light, camera, player, sprite, tile, player2d, camera2d, decal, canvas ... see reference_components) and optionally sets name, transform and any properties in one step. Returns the actor with its id.",
        Schema({{"kind", Str("Spawn kind, e.g. cube, sphere, sprite, camera2d, decal, canvas, empty.")}, {"name", Str("Actor name.")}, {"position", Vec(3, "World position.")},
                {"rotation", Vec(3, "Euler angles in degrees (x pitch, y yaw, z roll).")}, {"scale", Vec(3, "Scale.")},
                {"properties", Object("More properties: {\"Component\": {\"Property\": value}}. Missing components are added, e.g. {\"Collider\": {}, \"Rigidbody\": {\"Gravity scale\": 2}}.")}},
               {"kind"}),
        false, false, [=](const Json& a) {
            Json r;
            ToolResult failure;
            if (!ask("spawn", a, r, failure)) return failure;
            std::string text = Pretty(r["actor"]);
            if (r.contains("problems")) text += "\n\nproblems:\n" + Pretty(r["problems"]);
            return TextResult(text, r.contains("problems"));
        });

    add("scene_set", "Set actor properties",
        "Changes properties of an actor through the undo stack. Either give component + property + value, or properties: {\"Transform\": {\"Position\": [0, 2, 0]}, \"Sprite\": {\"Texture\": \"Content/Textures/a.png\"}}. Also renames: {\"Name\": {\"Name\": \"Hero\"}}. Components the actor lacks are added.",
        Schema({{"actor", actorProp}, {"component", Str("Component name, e.g. Transform.")}, {"property", Str("Property name, e.g. Position.")}, {"value", AnyValue("The new value: number, bool, string, [x, y, z], enum name or \"#rrggbb\" for colors.")},
                {"properties", Object("Several changes at once: {\"Component\": {\"Property\": value}}.")}},
               {"actor"}),
        false, false, [=](const Json& a) {
            Json r;
            ToolResult failure;
            if (!ask("set", a, r, failure)) return failure;
            std::string text = Pretty(r["actor"]);
            if (r.contains("problems")) text += "\n\nproblems (the rest was applied):\n" + Pretty(r["problems"]);
            return TextResult(text, r.contains("problems"));
        });

    add("scene_add_component", "Add a component", "Adds a component (Camera, Collider, Rigidbody, Sprite, Decal, Canvas, Script, Directional Light, Mesh ... or one of the game module) to an actor, optionally with property values.",
        Schema({{"actor", actorProp}, {"component", Str("Component name.")}, {"properties", Object("Initial property values: {\"Property\": value}.")}}, {"actor", "component"}), false, false, [=](const Json& a) {
            Json r;
            ToolResult failure;
            if (!ask("add_component", a, r, failure)) return failure;
            std::string text = Pretty(r["actor"]);
            if (r.contains("problems")) text += "\n\nproblems:\n" + Pretty(r["problems"]);
            return TextResult(text, r.contains("problems"));
        });

    add("scene_remove_component", "Remove a component", "Removes a component from an actor (Transform and Name cannot be removed).",
        Schema({{"actor", actorProp}, {"component", Str("Component name.")}}, {"actor", "component"}), false, true, [=](const Json& a) {
            Json r;
            ToolResult failure;
            if (!ask("remove_component", a, r, failure)) return failure;
            return TextResult(Pretty(r["actor"]));
        });

    add("scene_delete", "Delete an actor", "Deletes an actor (undo restores it).", Schema({{"actor", actorProp}}, {"actor"}), false, true, [=](const Json& a) {
        Json r;
        ToolResult failure;
        if (!ask("delete", actorArgs(a), r, failure)) return failure;
        return TextResult("Deleted " + r.value("deleted", std::string()));
    });

    add("scene_duplicate", "Duplicate an actor", "Copies an actor with all its components.", Schema({{"actor", actorProp}}, {"actor"}), false, false, [=](const Json& a) {
        Json r;
        ToolResult failure;
        if (!ask("duplicate", actorArgs(a), r, failure)) return failure;
        return TextResult(Pretty(r["actor"]));
    });

    add("scene_select", "Select an actor", "Selects an actor in the editor (the gizmo, the Details panel and `focus` use the selection).", Schema({{"actor", actorProp}}, {"actor"}), false, false, [=](const Json& a) {
        Json r;
        ToolResult failure;
        if (!ask("select", actorArgs(a), r, failure)) return failure;
        return TextResult("Selected " + r["selected"].get<std::string>());
    });

    add("scene_focus", "Focus the camera on an actor", "Selects an actor and moves the editor camera to look at it.", Schema({{"actor", actorProp}}, {"actor"}), false, false, [=](const Json& a) {
        Json r;
        ToolResult failure;
        if (!ask("select", actorArgs(a), r, failure)) return failure;
        return exec("focus");
    });

    add("scene_undo", "Undo", "Undoes the last edit(s) of the editor.", Schema({{"count", Int("How many steps (default 1).")}}), false, true, [=](const Json& a) {
        std::string text;
        bool problems = false;
        for (int i = 0; i < std::clamp(a.value("count", 1), 1, 100); ++i) {
            const ToolResult r = exec("undo");
            text += r.content[0]["text"].get<std::string>() + "\n";
            problems = problems || r.isError;
        }
        return TextResult(text, problems);
    });

    add("scene_redo", "Redo", "Redoes what undo took back.", Schema({{"count", Int("How many steps (default 1).")}}), false, false, [=](const Json& a) {
        std::string text;
        for (int i = 0; i < std::clamp(a.value("count", 1), 1, 100); ++i) text += exec("redo").content[0]["text"].get<std::string>() + "\n";
        return TextResult(text);
    });

    add("scene_new", "New scene", "Starts an empty untitled scene. Refuses when the open scene has unsaved changes unless discard_changes is true.",
        Schema({{"discard_changes", Bool("Discard unsaved changes. Default false.")}}), false, true, [=](const Json& a) { return exec(a.value("discard_changes", false) ? "new force" : "new"); });

    add("scene_open", "Open a scene", "Opens a .scene file of the project. Refuses when the open scene has unsaved changes unless discard_changes is true.",
        Schema({{"path", Str("Project-relative path, e.g. Content/Scenes/Level1.scene.")}, {"discard_changes", Bool("Discard unsaved changes. Default false.")}}, {"path"}), false, true, [=](const Json& a) {
            return exec("open " + a.at("path").get<std::string>() + (a.value("discard_changes", false) ? " force" : ""));
        });

    add("scene_save", "Save the scene", "Saves the open scene; with a path it is saved under that name (Save As).",
        Schema({{"path", Str("Optional project-relative path for Save As, e.g. Content/Scenes/Level2.scene.")}}), false, false, [=](const Json& a) {
            return exec(a.contains("path") ? "saveas " + a["path"].get<std::string>() : "save");
        });

    // ---- the view ------------------------------------------------------------------------------------------------------------------------------

    add("viewport_screenshot", "Take a screenshot", "A PNG of the viewport (what the scene, the game or the UI preview looks like now) or of the whole editor window. Look at it after changes to check the result.",
        Schema({{"target", Enum({"viewport", "window"}, "viewport (default) or the whole editor window.")}}), true, false, [=](const Json& a) {
            Json r;
            ToolResult failure;
            if (!ask("screenshot", {{"target", a.value("target", std::string("viewport"))}}, r, failure, 60)) return failure;
            const fs::path file = r.value("path", std::string());
            std::string bytes;
            if (!r.value("saved", false) || !ReadFileText(file, bytes) || bytes.size() < 24) return Fail("the screenshot was not saved");
            auto be32 = [&](size_t at) { return (static_cast<unsigned char>(bytes[at]) << 24) | (static_cast<unsigned char>(bytes[at + 1]) << 16) | (static_cast<unsigned char>(bytes[at + 2]) << 8) | static_cast<unsigned char>(bytes[at + 3]); };
            return ImageResult(Base64Encode(bytes), "Screenshot " + std::to_string(be32(16)) + " x " + std::to_string(be32(20)) + " saved to " + file.generic_string());
        });

    add("viewport_camera", "Move the editor camera", "Positions the editor camera (3D view) or the flat 2D view, or switches between them. The camera looks along yaw/pitch (yaw -90 looks down -Z) or at a point.",
        Schema({{"mode", Enum({"3d", "2d"}, "Switch the viewport to the 3D or the flat 2D view.")}, {"position", Vec(3, "3D camera position.")}, {"yaw", Num("Degrees; -90 looks down -Z.")},
                {"pitch", Num("Degrees; negative looks down.")}, {"look_at", Vec(3, "Turn the 3D camera towards this point (after position).")},
                {"center", Vec(2, "2D view: the world point in the middle.")}, {"half_height", Num("2D view: half the height of the view in world units (zoom).")}}),
        false, false, [=](const Json& a) {
            std::string text;
            bool problems = false;
            auto run = [&](const std::string& line) {
                const ToolResult r = exec(line);
                problems = problems || r.isError;
                const std::string t = r.content[0]["text"].get<std::string>();
                if (t != "(done)") text += t + "\n";
            };
            const std::string mode = a.value("mode", std::string());
            if (mode == "3d") run("view 3d");
            if (a.contains("center") || mode == "2d") {
                const Json c = a.value("center", Json::array({0, 0}));
                run("view 2d " + std::to_string(c[0].get<double>()) + " " + std::to_string(c[1].get<double>()) + " " + std::to_string(a.value("half_height", 0.0)));
            }
            if (a.contains("position")) {
                const Json p = a["position"];
                std::string line = "camera " + std::to_string(p[0].get<double>()) + " " + std::to_string(p[1].get<double>()) + " " + std::to_string(p[2].get<double>());
                if (a.contains("yaw") && a.contains("pitch")) line += " " + std::to_string(a["yaw"].get<double>()) + " " + std::to_string(a["pitch"].get<double>());
                run(line);
            } else if (a.contains("yaw") || a.contains("pitch")) {
                return Fail("give 'position' together with 'yaw' and 'pitch'");
            }
            if (a.contains("look_at")) {
                const Json p = a["look_at"];
                run("lookat " + std::to_string(p[0].get<double>()) + " " + std::to_string(p[1].get<double>()) + " " + std::to_string(p[2].get<double>()));
            }
            if (text.empty()) run("camera");
            return TextResult(text, problems);
        });

    // ---- playing --------------------------------------------------------------------------------------------------------------------------------

    add("play_start", "Play", "Starts the game in the editor (Lua scripts, physics, UI). The scene comes back as it was when play stops.", Schema(), false, false, [=](const Json&) { return exec("play"); });
    add("play_stop", "Stop playing", "Stops the game and restores the scene as it was before play.", Schema(), false, false, [=](const Json&) { return exec("stop"); });
    add("play_pause", "Pause or resume", "Pauses or resumes the running game (the toolbar's Pause button).", Schema({{"paused", Bool("true to pause, false to resume.")}}, {"paused"}), false, false, [=](const Json& a) {
        return exec(a.at("paused").get<bool>() ? "pause" : "resume");
    });

    add("play_wait", "Wait for frames", "Lets the editor (and a running game) run for a number of frames, about 60 per second. Use it between inputs and screenshots.",
        Schema({{"frames", Int("Frames to wait (1..36000).")}}, {"frames"}), false, false, [=](const Json& a) {
            Json r;
            ToolResult failure;
            const int frames = std::clamp(a.at("frames").get<int>(), 1, 36000);
            if (!ask("wait", {{"frames", frames}}, r, failure, 30 + frames / 20)) return failure;
            return TextResult("Waited " + std::to_string(frames) + " frames.");
        });

    add("play_keys", "Press game keys", "Presses keys for the running game (Input in scripts: input.key / input.pressed). tap = press and release, hold = keep down for some frames, down / up for manual control. Keys: a-z, 0-9, space, shift, ctrl, alt, tab, enter, escape, up, down, left, right.",
        Schema({{"keys", ArrayOf({{"type", "string"}}, "Key names.")}, {"action", Enum({"tap", "hold", "down", "up"}, "Default tap.")}, {"frames", Int("For hold: how many frames to keep the keys down (default 30).")}}, {"keys"}), false, false, [=](const Json& a) {
            const std::string action = a.value("action", std::string("tap"));
            std::string text;
            bool problems = false;
            auto run = [&](const std::string& line) {
                const ToolResult r = exec(line);
                problems = problems || r.isError;
                const std::string t = r.content[0]["text"].get<std::string>();
                if (t != "(done)") text += t + "\n";
            };
            for (const Json& k : a.at("keys")) run("key " + k.get<std::string>() + " " + (action == "hold" ? "down" : action));
            if (action == "hold") {
                Json r;
                ToolResult failure;
                const int frames = std::clamp(a.value("frames", 30), 1, 36000);
                if (!ask("wait", {{"frames", frames}}, r, failure, 30 + frames / 20)) return failure;
                for (const Json& k : a.at("keys")) run("key " + k.get<std::string>() + " up");
            }
            return TextResult(text.empty() ? "Done." : text, problems);
        });

    add("play_mouse", "Move and click the mouse", "Moves the mouse over the game view and presses or releases the left button (UI buttons, sliders, input.mouse in scripts). Positions are fractions of the viewport (0,0 top left, 1,1 bottom right) unless space is window.",
        Schema({{"x", Num("Horizontal position.")}, {"y", Num("Vertical position.")}, {"space", Enum({"viewport", "window"}, "Default viewport (fractions 0..1); window = window pixels.")},
                {"action", Enum({"move", "down", "up", "click", "release_control"}, "move (default), down, up, click (down, a few frames, up) or release_control (give the mouse back to the real cursor).")}}),
        false, false, [=](const Json& a) {
            const std::string action = a.value("action", std::string("move"));
            if (action == "release_control") return exec("mouse 0 0 off");
            Json status;
            ToolResult failure;
            if (!ask("status", Json::object(), status, failure)) return failure;
            double x = a.value("x", 0.5), y = a.value("y", 0.5);
            if (a.value("space", std::string("viewport")) == "viewport") {
                const Json rect = status["viewport_rect"];
                x = rect[0].get<double>() + x * rect[2].get<double>();
                y = rect[1].get<double>() + y * rect[3].get<double>();
            }
            auto at = [&](const char* state) { return "mouse " + std::to_string(x) + " " + std::to_string(y) + (state ? std::string(" ") + state : std::string()); };
            std::string text;
            auto run = [&](const std::string& line) {
                const ToolResult r = exec(line);
                const std::string t = r.content[0]["text"].get<std::string>();
                if (t != "(done)") text += t + "\n";
            };
            if (action == "click") {
                run(at(nullptr));
                Json w;
                ToolResult f2;
                ask("wait", {{"frames", 3}}, w, f2);
                run(at("down"));
                ask("wait", {{"frames", 4}}, w, f2);
                run(at("up"));
                ask("wait", {{"frames", 3}}, w, f2);
            } else {
                run(at(action == "move" ? nullptr : action.c_str()));
            }
            return TextResult(text.empty() ? "Done." : text);
        });

    add("play_lua", "Run Lua in the game", "Runs Lua code in the running game's script VM (the same globals scripts use: world, input, ui, hud, game ...; use log(...) or print(...) to see values in the result). When nothing is playing it runs in the UI preview instead.",
        Schema({{"code", Str("Lua code, may span several lines.")}, {"target", Enum({"game", "ui"}, "game (needs a running game) or ui (the preview). Default: game when playing, else ui.")}}, {"code"}), false, false, [=](const Json& a) {
            Json r;
            ToolResult failure;
            Json args = {{"code", a.at("code")}};
            if (a.contains("target")) args["target"] = a["target"];
            if (!ask("lua", args, r, failure)) return failure;
            std::string text = r.value("text", std::string());
            const bool error = r.contains("error");
            if (error) text += (text.empty() ? "" : "\n") + std::string("Lua error: ") + r["error"].get<std::string>();
            return TextResult(text.empty() ? "(done, no output)" : text, error);
        });

    // ---- UI ------------------------------------------------------------------------------------------------------------------------------------------

    add("ui_tree", "UI tree", "The canvases and nodes of the running game's UI (or of the preview): kind, name, rectangle in UI units, hidden / hovered / pressed flags and texts.", Schema(), true, false, [=](const Json&) { return exec("ui tree"); });
    add("ui_lua", "Run Lua in the UI", "Runs Lua code against the UI (ui.find('Score').text = 'x', ui.toast('Hi'), node:animate{...}); in the running game or, when nothing plays, in the editor preview.",
        Schema({{"code", Str("Lua code.")}}, {"code"}), false, false, [=](const Json& a) {
            Json r;
            ToolResult failure;
            if (!ask("lua", {{"code", a.at("code")}}, r, failure)) return failure;
            std::string text = r.value("text", std::string());
            const bool error = r.contains("error");
            if (error) text += (text.empty() ? "" : "\n") + std::string("Lua error: ") + r["error"].get<std::string>();
            return TextResult(text.empty() ? "(done)" : text, error);
        });
    add("ui_select", "Select a UI node", "Selects a UI node by name in the editor's UI panel; it is outlined in the viewport.", Schema({{"node", Str("Node name.")}}, {"node"}), false, false, [=](const Json& a) {
        return exec("ui select " + a.at("node").get<std::string>());
    });

    // ---- project files -----------------------------------------------------------------------------------------------------------------------------------

    add("project_info", "Project info", "Where the project is, which folders it has, and the engine folder. Works without the editor.", Schema(), true, false, [&link](const Json&) {
        Json out = {{"project", link.Project().generic_string()}, {"engine", link.EngineRoot().generic_string()}, {"editor_connected", link.Connected()}};
        Json folders = Json::array();
        std::error_code ec;
        if (fs::is_directory(link.Project(), ec)) {
            for (const auto& e : fs::directory_iterator(link.Project(), ec)) {
                if (e.is_directory()) folders.push_back(e.path().filename().string() + "/");
                else folders.push_back(e.path().filename().string());
            }
        }
        out["entries"] = folders;
        return TextResult(Pretty(out));
    });

    add("project_files", "List project files", "Lists files of the project (Content/Scenes, Content/Scripts, Content/Shaders, Content/Materials, Content/UI, Content/Textures, Content/Pipelines, Plugins, Game). Paths are project-relative; Engine/... lists the engine's own files (shaders, Lua DSL, fonts).",
        Schema({{"folder", Str("Folder to list, default the project root (use Content for assets, Engine/Shaders for the engine's).")}, {"contains", Str("Only paths that contain this text.")}, {"max_depth", Int("Default 6.")}}), true, false, [&link](const Json& a) {
            ResolvedPath base;
            std::string error;
            const std::string folder = a.value("folder", std::string("."));
            if (folder == "." || folder.empty()) {
                base.full = link.Project();
            } else if (!ResolvePath(link, folder, false, base, error)) {
                return Fail(error);
            }
            std::error_code ec;
            if (!fs::is_directory(base.full, ec)) return Fail("not a folder: " + folder);
            const std::string needle = Lower(a.value("contains", std::string()));
            const int maxDepth = a.value("max_depth", 6);
            std::ostringstream out;
            int count = 0;
            for (auto it = fs::recursive_directory_iterator(base.full, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                const std::string name = it->path().filename().string();
                if (it.depth() >= maxDepth) it.disable_recursion_pending();
                if (it->is_directory() && (name == "Saved" || name == "build" || name == ".git" || name == "GameBuild")) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_directory()) continue;
                std::string shown = (base.engine ? fs::relative(it->path(), link.EngineRoot(), ec) : fs::relative(it->path(), link.Project(), ec)).generic_string();
                if (!needle.empty() && Lower(shown).find(needle) == std::string::npos) continue;
                out << shown << "  " << it->file_size(ec) << " B\n";
                if (++count >= 2000) {
                    out << "... (more than 2000 files, narrow it down with 'folder' or 'contains')\n";
                    break;
                }
            }
            return TextResult(count == 0 ? "(no files)" : out.str());
        });

    add("project_read", "Read a project file", "Reads a text file of the project (Lua scripts, shaders, materials, UI documents, scenes, pipelines, C++ game code) or of the engine (Engine/Lua/shader_dsl.lua, Engine/Shaders/*.lua). PNG images are returned as images.",
        Schema({{"path", Str("Project-relative path, e.g. Content/Scripts/platformer.lua or Engine/Shaders/sprite.lua.")}, {"start_line", Int("First line to return (1-based).")}, {"max_lines", Int("How many lines (default 2000).")}}, {"path"}), true, false, [&link](const Json& a) {
            ResolvedPath p;
            std::string error;
            if (!ResolvePath(link, a.at("path").get<std::string>(), false, p, error)) return Fail(error);
            std::error_code ec;
            if (!fs::is_regular_file(p.full, ec)) return Fail("no such file: " + p.relative.generic_string());
            std::string bytes;
            if (!ReadFileText(p.full, bytes)) return Fail("cannot read " + p.relative.generic_string());
            if (IsImagePath(p.full)) {
                if (bytes.size() > 3 * 1024 * 1024) return Fail("the image is larger than 3 MB");
                return ImageResult(Base64Encode(bytes), p.relative.generic_string() + " (" + std::to_string(bytes.size()) + " bytes)");
            }
            if (LooksBinary(bytes)) return Fail(p.relative.generic_string() + " is a binary file (" + std::to_string(bytes.size()) + " bytes)");
            if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) bytes.erase(0, 3);
            const int start = std::max(1, a.value("start_line", 1)), maxLines = std::clamp(a.value("max_lines", 2000), 1, 20000);
            std::istringstream in(bytes);
            std::ostringstream out;
            std::string line;
            int number = 0, written = 0;
            bool more = false;
            while (std::getline(in, line)) {
                ++number;
                if (number < start) continue;
                if (written >= maxLines) {
                    more = true;
                    break;
                }
                if (!line.empty() && line.back() == '\r') line.pop_back();
                out << line << "\n";
                ++written;
            }
            std::string text = out.str();
            if (more) text += "... (cut: use start_line to continue)\n";
            return TextResult(text.empty() ? "(empty file)" : text);
        });

    // Writes a file, then (when an editor runs) lets it reload and returns the compile/script errors it logged.
    auto writeFile = [&link, ask, logSince](const std::string& path, const std::string& content, bool createOnly) -> ToolResult {
        ResolvedPath p;
        std::string error;
        if (!ResolvePath(link, path, true, p, error)) return Fail(error);
        std::error_code ec;
        const bool existed = fs::exists(p.full, ec);
        if (createOnly && existed) return Fail(p.relative.generic_string() + " already exists");
        uint64_t mark = 0;
        const bool editor = link.AttachIfRunning();
        if (editor) {
            Json r;
            ToolResult ignored;
            if (ask("log", {{"max", 1}}, r, ignored)) mark = r.value("total", static_cast<uint64_t>(0));
        }
        fs::create_directories(p.full.parent_path(), ec);
        {
            std::ofstream out(p.full, std::ios::binary | std::ios::trunc);
            if (!out) return Fail("cannot write " + p.relative.generic_string());
            out << content;
        }
        std::string text = std::string(existed ? "Wrote " : "Created ") + p.relative.generic_string() + " (" + std::to_string(content.size()) + " bytes).";
        const std::string ext = Lower(p.relative.extension().string());
        bool problems = false;
        if (editor) {
            Json r;
            ToolResult ignored;
            if (ext == ".scene") {
                text += " A scene file is read when it is opened: use scene_open.";
            } else {
                ask("wait", {{"frames", 3}, {"seconds", 1.2}}, r, ignored, 30); // shaders, materials, pipelines, UI documents and scripts reload by themselves
                const std::string log = logSince(mark, true);
                if (!log.empty()) {
                    text += "\nThe editor reported:\n" + log;
                    problems = true;
                } else if (ext == ".lua" || ext == ".png") {
                    text += " The editor reloaded it without problems.";
                }
            }
        } else {
            text += " (The editor is not running; it loads the file when it starts.)";
        }
        return TextResult(text, problems);
    };

    add("project_write", "Write a project file", "Creates or overwrites a file of the project: Lua scripts (Content/Scripts), shaders, materials, UI documents (Content/UI), pipelines (Content/Pipelines), scenes, C++ game code. Folders are created. Lua files hot-reload in the running editor and the answer includes the errors the editor logged. Use encoding base64 for binary files such as PNG.",
        Schema({{"path", Str("Project-relative path.")}, {"content", Str("The whole new content of the file.")}, {"encoding", Enum({"utf8", "base64"}, "Default utf8.")}, {"create_only", Bool("Fail when the file already exists.")}}, {"path", "content"}),
        false, true, [writeFile](const Json& a) {
            std::string content = a.at("content").get<std::string>();
            if (a.value("encoding", std::string("utf8")) == "base64") {
                bool ok = false;
                content = Base64Decode(content, ok);
                if (!ok) return Fail("the content is not valid base64");
            }
            return writeFile(a.at("path").get<std::string>(), content, a.value("create_only", false));
        });

    add("project_edit", "Edit a project file", "Replaces one piece of text in a file (like a find and replace that must match exactly once). Cheaper than rewriting the whole file.",
        Schema({{"path", Str("Project-relative path.")}, {"find", Str("The exact text to replace; must occur exactly once.")}, {"replace", Str("The new text.")}}, {"path", "find", "replace"}), false, true, [&link, writeFile](const Json& a) {
            ResolvedPath p;
            std::string error;
            if (!ResolvePath(link, a.at("path").get<std::string>(), true, p, error)) return Fail(error);
            std::string text;
            if (!ReadFileText(p.full, text)) return Fail("cannot read " + p.relative.generic_string());
            const std::string find = a.at("find").get<std::string>();
            if (find.empty()) return Fail("'find' is empty");
            const size_t first = text.find(find);
            if (first == std::string::npos) return Fail("the text to replace was not found in " + p.relative.generic_string());
            if (text.find(find, first + 1) != std::string::npos) return Fail("the text to replace occurs more than once; make 'find' longer so it is unique");
            text.replace(first, find.size(), a.at("replace").get<std::string>());
            return writeFile(a.at("path").get<std::string>(), text, false);
        });

    add("project_delete", "Delete a project file", "Deletes a file of the project (or an empty folder).", Schema({{"path", Str("Project-relative path.")}}, {"path"}), false, true, [&link](const Json& a) {
        ResolvedPath p;
        std::string error;
        if (!ResolvePath(link, a.at("path").get<std::string>(), true, p, error)) return Fail(error);
        std::error_code ec;
        if (!fs::exists(p.full, ec)) return Fail("no such file: " + p.relative.generic_string());
        if (fs::is_directory(p.full, ec) && !fs::is_empty(p.full, ec)) return Fail("the folder is not empty; delete its files first");
        fs::remove(p.full, ec);
        if (ec) return Fail("could not delete: " + ec.message());
        return TextResult("Deleted " + p.relative.generic_string());
    });

    add("project_move", "Move or rename a project file", "Moves or renames a file of the project. References inside other files are not updated.", Schema({{"from", Str("Project-relative path.")}, {"to", Str("New project-relative path.")}}, {"from", "to"}), false, true, [&link](const Json& a) {
        ResolvedPath from, to;
        std::string error;
        if (!ResolvePath(link, a.at("from").get<std::string>(), true, from, error) || !ResolvePath(link, a.at("to").get<std::string>(), true, to, error)) return Fail(error);
        std::error_code ec;
        if (!fs::exists(from.full, ec)) return Fail("no such file: " + from.relative.generic_string());
        if (fs::exists(to.full, ec)) return Fail(to.relative.generic_string() + " already exists");
        fs::create_directories(to.full.parent_path(), ec);
        fs::rename(from.full, to.full, ec);
        if (ec) return Fail("could not move: " + ec.message());
        return TextResult("Moved " + from.relative.generic_string() + " to " + to.relative.generic_string());
    });

    add("project_create_asset", "Create an asset from a template", "Creates a starter file: script (gameplay Lua), shader (surface shader), material (a Lua material for a shader), ui_document (a UI screen), scene (empty), graph_shader (node-graph shader, needs the editor).",
        Schema({{"kind", Enum({"script", "shader", "material", "ui_document", "scene", "graph_shader"}, "What to create.")}, {"path", Str("Project-relative path, e.g. Content/Scripts/enemy.lua.")},
                {"shader", Str("For material: the shader path, default Content/Shaders/default_lit.lua.")}}, {"kind", "path"}),
        false, false, [writeFile, exec](const Json& a) {
            const std::string kind = a.at("kind").get<std::string>(), path = a.at("path").get<std::string>();
            if (kind == "script") return writeFile(path, kScriptTemplate, true);
            if (kind == "shader") return writeFile(path, kShaderTemplate, true);
            if (kind == "ui_document") return writeFile(path, kUiTemplate, true);
            if (kind == "scene") return writeFile(path, "{\n  \"magnumScene\": 1,\n  \"actors\": []\n}\n", true);
            if (kind == "material") {
                return writeFile(path, "-- Magnum material.\nreturn material {\n    shader = \"" + a.value("shader", std::string("Content/Shaders/default_lit.lua")) + "\",\n    params = {\n    },\n}\n", true);
            }
            if (kind == "graph_shader") return exec("graph new " + path);
            return Fail("unknown kind '" + kind + "'");
        });

    // ---- shaders, materials, pipeline --------------------------------------------------------------------------------------------------------------------

    add("shader_reload", "Reload shaders and materials", "Recompiles every shader and material (they also reload by themselves when a file is saved). Returns compile errors with the Lua line.", Schema(), false, false, [=](const Json&) { return exec("shader reload"); });

    add("material_set", "Set a material parameter", "Sets a parameter of a material file through the editor (undoable, the file is rewritten). Numbers or a texture path.",
        Schema({{"material", Str("Material path, e.g. Content/Materials/Red.lua.")}, {"param", Str("Parameter name of the material's shader.")}, {"value", AnyValue("A number, an array of numbers (vec3 / color) or a texture path.")}}, {"material", "param", "value"}),
        false, false, [=](const Json& a) {
            const ToolResult first = exec("inspect " + a.at("material").get<std::string>());
            std::string value;
            const Json v = a.at("value");
            if (v.is_array()) {
                for (const Json& n : v) value += (value.empty() ? "" : " ") + std::to_string(n.get<double>());
            } else if (v.is_number()) {
                value = std::to_string(v.get<double>());
            } else {
                value = v.get<std::string>();
            }
            return exec("matset " + a.at("param").get<std::string>() + " " + value);
        });

    add("graph_command", "Edit a graph shader", "Runs a node-graph editor command on the open graph: new <path> | open <path> | add <type> [x y] | link <src> <dst> <pin> | unlink <dst> <pin> | pin <node> <pin> <value> | delete <id> | list.",
        Schema({{"command", Str("The part after `graph`, e.g. add Multiply 300 200.")}}, {"command"}), false, false, [=](const Json& a) { return exec("graph " + a.at("command").get<std::string>()); });

    // ---- the game module and tests ---------------------------------------------------------------------------------------------------------------------------

    add("game_build", "Build the game module", "Builds the project's C++ game module (Game/src) and waits for the result; the module reloads into the editor when it is built.",
        Schema({{"timeout_seconds", Int("How long to wait (default 600).")}}), false, false, [=](const Json& a) {
            ToolResult started = exec("build");
            Json r;
            ToolResult failure;
            const int timeout = std::clamp(a.value("timeout_seconds", 600), 10, 3600);
            if (!ask("wait", {{"frames", 5}, {"build", true}}, r, failure, timeout)) return failure;
            const ToolResult status = exec("game");
            return TextResult(started.content[0]["text"].get<std::string>() + "\n" + status.content[0]["text"].get<std::string>(), status.isError);
        });
    add("game_status", "Game module status", "Build state and the components the game module registered.", Schema(), true, false, [=](const Json&) { return exec("game"); });

    add("run_tests", "Run the engine's tests", "Runs MagnumTests.exe (physics, scripts, shaders, UI, sprites ...) and returns the end of its output. A filter runs only the tests whose name contains it. Takes about a minute for all.",
        Schema({{"filter", Str("Part of a test name, e.g. Ui or Platformer.")}, {"timeout_seconds", Int("Default 600.")}}), true, false, [&link](const Json& a) {
            const fs::path exe = link.ExeDir() / "MagnumTests.exe";
            std::error_code ec;
            if (!fs::exists(exe, ec)) return Fail("MagnumTests.exe was not found next to the server; build the engine first");
            std::string output, error;
            int code = 0;
            const bool finished = RunProgram(exe, a.value("filter", std::string()), link.EngineRoot(), std::clamp(a.value("timeout_seconds", 600), 10, 3600), output, code, error);
            std::string text = Tail(output, 60);
            if (!finished) text += "\n" + error;
            return TextResult(text + "\nexit code " + std::to_string(code), !finished || code != 0);
        });

    // ---- documentation ------------------------------------------------------------------------------------------------------------------------------------------

    add("docs_read", "Read the engine docs", "The engine's documentation: UI.md (UI system and Lua UI API), 2D.md (sprites, 2D games, decals), MCP.md (this server), CHANGELOG.md. Call without a name to list them.",
        Schema({{"name", Str("File name, e.g. UI.md. Omit to list.")}}), true, false, [this](const Json& a) {
            const auto docs = Docs();
            if (!a.contains("name")) {
                std::string text = "Available documents:\n";
                for (const auto& [name, path] : docs) text += "- " + name + "\n";
                return TextResult(text);
            }
            const std::string want = Lower(a["name"].get<std::string>());
            for (const auto& [name, path] : docs) {
                if (Lower(name) == want || Lower(name) == want + ".md") {
                    std::string text;
                    if (!ReadFileText(path, text)) return Fail("cannot read " + name);
                    return TextResult(text);
                }
            }
            return Fail("no such document; call docs_read without a name to list them");
        });
}

} // namespace magnum::mcp
