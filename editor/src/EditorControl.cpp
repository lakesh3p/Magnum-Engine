// The editor side of the control channel (see ControlServer.h): what each "op" of a request does. Everything here runs on
// the main thread, between frames, so it can touch the scene and the panels like a console command does.
#include "EditorApp.h"

#include "commands/EditorCommands.h"
#include "core/Log.h"
#include "panels/PanelUtil.h"
#include "scene/Reflection.h"
#include "scene/SceneSerializer.h"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace magnum::editor {

namespace {

using Json = nlohmann::json;

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "Directional_Light", "directional light" and "Directional Light" all name the same component.
std::string Normalize(std::string s) {
    std::replace(s.begin(), s.end(), '_', ' ');
    return Lower(Trim(s));
}

std::string IdText(uint64_t id) { return std::format("{:016x}", id); }

const ComponentInfo* FindComponent(const std::string& name) {
    const std::string wanted = Normalize(name);
    for (const ComponentInfo* c : AllComponentInfos()) {
        if (Normalize(c->name) == wanted) return c;
    }
    return nullptr;
}

int FindProperty(const ComponentInfo& info, const std::string& name) {
    const std::string wanted = Normalize(name);
    for (size_t i = 0; i < info.props.size(); ++i) {
        if (Normalize(info.props[i].name) == wanted) return static_cast<int>(i);
    }
    return -1;
}

const char* KindName(PropKind k) {
    switch (k) {
    case PropKind::Bool: return "bool";
    case PropKind::Int: return "int";
    case PropKind::Float: return "float";
    case PropKind::Vec3: return "vec3";
    case PropKind::Color3: return "color";
    case PropKind::String:
    case PropKind::CString: return "string";
    case PropKind::Enum: return "enum";
    }
    return "?";
}

Json ValueToJson(const PropertyInfo& p, const PropertyValue& v) {
    switch (p.kind) {
    case PropKind::Bool: return std::get<bool>(v);
    case PropKind::Int: return std::get<int>(v);
    case PropKind::Float: return std::get<float>(v);
    case PropKind::Vec3:
    case PropKind::Color3: {
        const glm::vec3& c = std::get<glm::vec3>(v);
        return Json::array({c.x, c.y, c.z});
    }
    case PropKind::String:
    case PropKind::CString: return std::get<std::string>(v);
    case PropKind::Enum: {
        const int i = std::get<int>(v);
        return i >= 0 && i < static_cast<int>(p.options.size()) ? Json(p.options[static_cast<size_t>(i)]) : Json(i);
    }
    }
    return nullptr;
}

bool Hex(const std::string& text, glm::vec3& out) {
    std::string s = text;
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6 || !std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c); })) return false;
    out = {static_cast<float>(std::stoi(s.substr(0, 2), nullptr, 16)) / 255.0f, static_cast<float>(std::stoi(s.substr(2, 2), nullptr, 16)) / 255.0f,
           static_cast<float>(std::stoi(s.substr(4, 2), nullptr, 16)) / 255.0f};
    return true;
}

bool JsonToValue(const PropertyInfo& p, const Json& j, PropertyValue& out, std::string& error) {
    auto number = [&](const Json& v, float& f) {
        if (v.is_number()) {
            f = v.get<float>();
            return true;
        }
        return false;
    };
    switch (p.kind) {
    case PropKind::Bool:
        if (j.is_boolean()) out = j.get<bool>();
        else if (j.is_number()) out = j.get<double>() != 0.0;
        else if (j.is_string()) out = Lower(j.get<std::string>()) == "true" || j.get<std::string>() == "1";
        else break;
        return true;
    case PropKind::Int:
        if (!j.is_number()) break;
        out = static_cast<int>(std::lround(j.get<double>()));
        return true;
    case PropKind::Float: {
        float f;
        if (!number(j, f)) break;
        out = f;
        return true;
    }
    case PropKind::Vec3:
    case PropKind::Color3: {
        glm::vec3 v(0.0f);
        if (j.is_array() && j.size() == 3 && number(j[0], v.x) && number(j[1], v.y) && number(j[2], v.z)) {
            out = v;
            return true;
        }
        if (j.is_object() && (number(j.value("x", Json()), v.x) || number(j.value("r", Json()), v.x))) {
            const char* keys[2][3] = {{"x", "y", "z"}, {"r", "g", "b"}};
            const int set = j.contains("x") ? 0 : 1;
            number(j.value(keys[set][1], Json()), v.y);
            number(j.value(keys[set][2], Json()), v.z);
            out = v;
            return true;
        }
        if (p.kind == PropKind::Color3 && j.is_string() && Hex(j.get<std::string>(), v)) {
            out = v;
            return true;
        }
        error = std::string("needs ") + (p.kind == PropKind::Color3 ? "a color [r, g, b] (0..1) or \"#rrggbb\"" : "a vector [x, y, z]");
        return false;
    }
    case PropKind::String:
    case PropKind::CString:
        if (!j.is_string()) break;
        out = j.get<std::string>();
        return true;
    case PropKind::Enum: {
        if (j.is_number()) {
            const int i = static_cast<int>(j.get<double>());
            if (i >= 0 && i < static_cast<int>(p.options.size())) {
                out = i;
                return true;
            }
        } else if (j.is_string()) {
            for (size_t i = 0; i < p.options.size(); ++i) {
                if (Lower(j.get<std::string>()) == Lower(p.options[i])) {
                    out = static_cast<int>(i);
                    return true;
                }
            }
        }
        std::string options;
        for (const char* o : p.options) options += (options.empty() ? "" : ", ") + std::string(o);
        error = "must be one of: " + options;
        return false;
    }
    }
    error = std::string("needs a value of kind ") + KindName(p.kind);
    return false;
}

Json ComponentJson(const ComponentInfo& info, void* data, bool withMeta) {
    Json props = Json::object();
    for (const PropertyInfo& p : info.props) {
        if (p.hidden) continue;
        props[p.name] = ValueToJson(p, GetProperty(p, data));
    }
    (void)withMeta;
    return props;
}

} // namespace

// ---- starting and describing the control channel (Tools > MCP, console `mcp`) ----------------------------------------------------------

namespace {

std::filesystem::path McpExe() {
    wchar_t buffer[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
    return std::filesystem::path(std::wstring(buffer, n)).parent_path() / "MagnumMcp.exe";
}

} // namespace

McpInfo EditorApp::McpInfoNow() const {
    McpInfo info;
    info.running = m_control.Running();
    info.port = info.running ? m_control.Port() : 0;
    info.client = info.running && m_control.HasClient();
    const std::filesystem::path exe = McpExe();
    std::error_code ec;
    info.serverPath = exe.string();
    info.serverBuilt = std::filesystem::exists(exe, ec);
    return info;
}

std::string EditorApp::McpStatus() const {
    std::string text;
    if (m_control.Running()) {
        text = std::format("Control channel: on, 127.0.0.1:{}, {}\n", m_control.Port(), m_control.HasClient() ? "an AI client is connected" : "waiting for a client");
    } else {
        text = "Control channel: off (start it with Tools > MCP, or let the AI client open the editor)\n";
    }
    std::error_code ec;
    const std::filesystem::path exe = McpExe();
    text += std::format("Server: {}{}", exe.generic_string(), std::filesystem::exists(exe, ec) ? "" : "  (not built: run build.bat Release)");
    return text;
}

void EditorApp::McpCommand(const std::string& args) {
    std::istringstream in(args);
    std::string sub, what;
    in >> sub >> what;
    sub = Lower(sub);
    const std::string exe = McpExe().string();
    const std::string project = m_ctx.projectRoot.string();
    const std::string claude = "claude mcp add magnum -- \"" + exe + "\" --project \"" + project + "\"";
    const Json config = {{"mcpServers", {{"magnum", {{"command", exe}, {"args", Json::array({"--project", project})}}}}}};
    if (sub == "start") {
        if (m_control.Running()) {
            Log::Info("The control channel is already on (port {})", m_control.Port());
        } else {
            std::string error;
            if (!m_control.Start(0, m_ctx.projectRoot / "Saved" / "control.json", m_ctx.projectRoot.generic_string(), error)) Log::Error("Control channel: {}", error);
        }
    } else if (sub == "stop") {
        m_control.Stop();
        Log::Info("The control channel is off");
    } else if (sub == "config") {
        Log::Info("Claude Code:  {}", claude);
        Log::Info("JSON config (Claude Desktop and others):\n{}", config.dump(2));
        Log::Info("The server starts or attaches to this editor by itself; to let it attach to this window, start the channel first (mcp start).");
    } else if (sub == "copy") {
        const std::string text = Lower(what) == "json" ? config.dump(2) : claude;
        ImGui::SetClipboardText(text.c_str());
        Log::Info("Copied the {} setup to the clipboard", Lower(what) == "json" ? "JSON" : "Claude Code");
    } else if (sub.empty() || sub == "status") {
        std::istringstream lines(McpStatus());
        for (std::string line; std::getline(lines, line);) Log::Info("{}", line);
    } else {
        Log::Warn("Usage: mcp start | stop | status | config | copy claude | copy json");
    }
}

// ---- pumping -----------------------------------------------------------------------------------------------------------------

void EditorApp::PumpControl() {
    if (!m_control.Running() || m_frame < 3) return;
    if (m_controlPending.active) {
        if (m_controlPending.frames > 0) --m_controlPending.frames;
        const bool settled = m_controlPending.frames <= 0 && std::chrono::steady_clock::now() >= m_controlPending.until && m_scriptWait == 0 && !m_scriptWaitBuild && m_ctx.playRequest == EditorContext::PlayRequest::None &&
                             (!m_controlPending.screenshot || m_pendingShotTarget.empty()) && (!m_controlPending.build || !m_builder.Pending());
        if (!settled) return;
        FinishControl();
    }
    Json request;
    while (!m_controlPending.active && m_control.Poll(request)) HandleControl(request);
}

void EditorApp::FinishControl() {
    Json response = {{"id", m_controlPending.id}, {"ok", true}};
    if (m_controlPending.screenshot) {
        response["path"] = m_controlPending.path;
        std::error_code ec;
        response["saved"] = std::filesystem::exists(m_controlPending.path, ec);
    }
    if (m_controlPending.collectLog) {
        Json lines = Json::array();
        bool errors = false;
        std::string text;
        for (const LogEntry& e : Log::Since(m_controlPending.logMark)) {
            if (e.text.rfind("> ", 0) == 0) continue; // the echo of the command itself
            const char* level = e.level == LogLevel::Info ? "info" : (e.level == LogLevel::Warn ? "warn" : "error");
            errors = errors || e.level == LogLevel::Error;
            lines.push_back({{"level", level}, {"text", e.text}});
            text += (text.empty() ? "" : "\n") + std::string(e.level == LogLevel::Info ? "" : (e.level == LogLevel::Warn ? "warning: " : "error: ")) + e.text;
        }
        response["output"] = lines;
        response["text"] = text;
        response["had_errors"] = errors;
    }
    if (!m_controlPending.error.empty()) response["error"] = m_controlPending.error;
    if (m_controlPending.quit) m_window.RequestClose();
    m_control.Reply(response);
    m_controlPending = {};
}

// ---- helpers ------------------------------------------------------------------------------------------------------------------

namespace {

// Finds an actor: "actor" (or "name") is its name, or its id (the 16 hex digits of the scene file). ("id" of a request is the
// request's own number, so it is not used here.)
uint64_t ResolveActor(const Scene& scene, const Json& req, std::string& error) {
    std::string wanted;
    for (const char* key : {"actor", "name"}) {
        if (req.contains(key) && req[key].is_string()) {
            wanted = req[key].get<std::string>();
            break;
        }
    }
    if (wanted.empty()) {
        error = "give the actor as 'actor' (its name or id)";
        return 0;
    }
    for (const uint64_t id : scene.Order()) {
        if (scene.TryGet<NameComponent>(id)->name == wanted) return id;
    }
    if (wanted.size() == 16 && std::all_of(wanted.begin(), wanted.end(), [](unsigned char c) { return std::isxdigit(c); })) {
        const uint64_t id = std::stoull(wanted, nullptr, 16);
        if (scene.Exists(id)) return id;
    }
    for (const uint64_t id : scene.Order()) { // a case-insensitive match as a second chance
        if (Lower(scene.TryGet<NameComponent>(id)->name) == Lower(wanted)) return id;
    }
    error = "there is no actor named or numbered '" + wanted + "'";
    return 0;
}

Json ActorJson(const Scene& scene, uint64_t id, bool full) {
    entt::registry& reg = const_cast<Scene&>(scene).Registry();
    const entt::entity e = scene.Find(id);
    Json out = {{"id", IdText(id)}, {"name", scene.TryGet<NameComponent>(id)->name}};
    Json components = full ? Json::object() : Json::array();
    for (const ComponentInfo* c : AllComponentInfos()) {
        if (!c->has(reg, e)) continue;
        if (full) {
            if (c->hidden) continue;
            components[c->name] = ComponentJson(*c, c->get(reg, e), false);
        } else if (!c->hidden) {
            components.push_back(c->name);
        }
    }
    out["components"] = components;
    if (!full) {
        if (const TransformComponent* t = scene.TryGet<TransformComponent>(id)) out["position"] = Json::array({t->position.x, t->position.y, t->position.z});
    }
    return out;
}

} // namespace

// Applies {"Component": {"Property": value}} to an actor through the undo stack; adds missing components. Returns the problems.
static std::vector<std::string> ApplyJsonProperties(EditorContext& ctx, uint64_t id, const Json& props) {
    std::vector<std::string> problems;
    if (!props.is_object()) {
        problems.push_back("'properties' must be an object like {\"Transform\": {\"Position\": [0, 1, 0]}}");
        return problems;
    }
    for (const auto& [componentName, values] : props.items()) {
        const ComponentInfo* info = FindComponent(componentName);
        if (!info) {
            problems.push_back("unknown component '" + componentName + "'");
            continue;
        }
        const entt::entity e = ctx.scene.Find(id);
        if (!info->has(ctx.scene.Registry(), e)) {
            if (info->hidden || !info->removable) {
                problems.push_back("the actor has no " + std::string(info->name));
                continue;
            }
            AddComponentCommand(ctx, id, *info);
        }
        if (!values.is_object()) {
            problems.push_back(std::string(info->name) + " needs an object of properties");
            continue;
        }
        for (const auto& [propName, value] : values.items()) {
            const int index = FindProperty(*info, propName);
            if (index < 0) {
                problems.push_back(std::string(info->name) + " has no property '" + propName + "'");
                continue;
            }
            PropertyValue converted;
            std::string error;
            if (!JsonToValue(info->props[static_cast<size_t>(index)], value, converted, error)) {
                problems.push_back(std::string(info->name) + "." + info->props[static_cast<size_t>(index)].name + ": " + error);
                continue;
            }
            SetPropertyCommand(ctx, id, *info, static_cast<size_t>(index), converted);
        }
    }
    return problems;
}

// ---- the ops --------------------------------------------------------------------------------------------------------------------

void EditorApp::HandleControl(const nlohmann::json& req) {
    const Json id = req.contains("id") ? req["id"] : Json(nullptr);
    const std::string op = req.value("op", std::string());
    auto fail = [&](const std::string& message) { m_control.Reply({{"id", id}, {"ok", false}, {"error", message}}); };
    auto ok = [&](Json extra = Json::object()) {
        extra["id"] = id;
        extra["ok"] = true;
        m_control.Reply(extra);
    };
    auto settle = [&](int frames, bool collectLog) { // answer after the commands had their effect
        m_controlPending = {};
        m_controlPending.active = true;
        m_controlPending.id = id;
        m_controlPending.frames = frames;
        m_controlPending.collectLog = collectLog;
        m_controlPending.logMark = Log::Total();
    };
    auto actorWithProblems = [&](uint64_t actor, const std::vector<std::string>& problems, Json extra = Json::object()) {
        extra["actor"] = ActorJson(m_ctx.scene, actor, true);
        if (!problems.empty()) extra["problems"] = problems;
        ok(extra);
    };

    if (op == "status") {
        Json out = {{"project", m_ctx.projectRoot.generic_string()}, {"project_name", m_projectName}, {"scene", m_ctx.scenePath}, {"dirty", m_ctx.IsDirty()},
                    {"playing", m_ctx.playing}, {"paused", m_ctx.paused}, {"actors", m_ctx.scene.Order().size()}, {"frame", m_frame},
                    {"fps", ImGui::GetIO().Framerate}, {"game_status", m_ctx.gameStatus}, {"view_2d", m_viewport.View2D()},
                    {"viewport", Json::array({static_cast<int>(m_ctx.gameViewSize.x), static_cast<int>(m_ctx.gameViewSize.y)})},
                    {"viewport_rect", Json::array({m_ctx.gameViewOrigin.x, m_ctx.gameViewOrigin.y, m_ctx.gameViewSize.x, m_ctx.gameViewSize.y})}, {"version", "0.1"}};
        const EditorCamera& c = m_ctx.camera;
        out["camera"] = {{"position", Json::array({c.position.x, c.position.y, c.position.z})}, {"yaw", c.yaw}, {"pitch", c.pitch}};
        if (m_ctx.scene.Exists(m_ctx.selected)) out["selected"] = {{"id", IdText(m_ctx.selected)}, {"name", m_ctx.scene.TryGet<NameComponent>(m_ctx.selected)->name}};
        Json canvases = Json::array();
        if (m_ctx.activeUi) {
            for (const UiCanvas& canvas : m_ctx.activeUi->Canvases()) canvases.push_back({{"name", canvas.name}, {"document", canvas.document}, {"order", canvas.order}, {"visible", canvas.visible}});
        }
        out["ui_canvases"] = canvases;
        ok(out);
    } else if (op == "exec") {
        const std::string line = Trim(req.value("line", std::string()));
        if (line.empty()) return fail("'line' is empty");
        settle(1, true);
        ExecuteCommand(line);
    } else if (op == "lua") { // run Lua with its newlines: in the running game, or in the UI preview when nothing plays
        const std::string code = req.value("code", std::string());
        if (code.empty()) return fail("'code' is empty");
        settle(1, true);
        std::string error;
        const bool toGame = req.value("target", std::string(m_ctx.playing ? "game" : "ui")) == "game";
        if (toGame ? !m_gameplay.RunLua(code, error) : !m_uiPreview.Run(code, error)) {
            Log::Error("lua: {}", error);
            m_controlPending.error = error;
        }
    } else if (op == "wait") {
        settle(std::clamp(req.value("frames", 1), 1, 36000), false);
        m_controlPending.build = req.value("build", false);
        if (req.contains("seconds")) m_controlPending.until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(std::clamp(req.value("seconds", 0.0), 0.0, 600.0) * 1000.0));
    } else if (op == "log") {
        const uint64_t since = req.value("since", static_cast<uint64_t>(0));
        const size_t maxLines = static_cast<size_t>(std::clamp(req.value("max", 200), 1, 5000));
        Json lines = Json::array();
        std::vector<LogEntry> entries = since > 0 ? Log::Since(since) : Log::Snapshot();
        if (entries.size() > maxLines) entries.erase(entries.begin(), entries.end() - static_cast<std::ptrdiff_t>(maxLines));
        for (const LogEntry& e : entries) {
            lines.push_back({{"level", e.level == LogLevel::Info ? "info" : (e.level == LogLevel::Warn ? "warn" : "error")}, {"text", e.text}});
        }
        ok({{"total", Log::Total()}, {"entries", lines}});
    } else if (op == "reflect") {
        Json components = Json::array();
        for (const ComponentInfo* c : AllComponentInfos()) {
            if (c->hidden && std::string(c->name) != "Name") continue;
            Json props = Json::array();
            for (const PropertyInfo& p : c->props) {
                if (p.hidden) continue;
                Json prop = {{"name", p.name}, {"kind", KindName(p.kind)}, {"group", p.category}};
                if (p.kind == PropKind::Enum) {
                    Json options = Json::array();
                    for (const char* o : p.options) options.push_back(o);
                    prop["options"] = options;
                }
                if (p.hasRange) prop["range"] = Json::array({p.min, p.max});
                props.push_back(prop);
            }
            components.push_back({{"name", c->name}, {"removable", c->removable}, {"from_game_module", c->game}, {"properties", props}});
        }
        Json kinds = Json::array();
        for (int i = 0; i < static_cast<int>(SpawnKind::Count); ++i) {
            const SpawnInfo& info = GetSpawnInfo(static_cast<SpawnKind>(i));
            kinds.push_back({{"name", info.name}, {"kind", info.command}});
        }
        ok({{"components", components}, {"spawn_kinds", kinds}});
    } else if (op == "scene") {
        const std::string filter = Lower(req.value("filter", std::string()));
        const size_t limit = static_cast<size_t>(std::clamp(req.value("limit", 500), 1, 100000));
        Json actors = Json::array();
        size_t total = 0;
        for (const uint64_t actor : m_ctx.scene.Order()) {
            Json a = ActorJson(m_ctx.scene, actor, false);
            if (!filter.empty()) {
                bool match = Lower(a["name"].get<std::string>()).find(filter) != std::string::npos;
                for (const Json& c : a["components"]) match = match || Lower(c.get<std::string>()).find(filter) != std::string::npos;
                if (!match) continue;
            }
            ++total;
            if (actors.size() < limit) actors.push_back(std::move(a));
        }
        ok({{"scene", m_ctx.scenePath}, {"dirty", m_ctx.IsDirty()}, {"matching", total}, {"returned", actors.size()}, {"actors", actors}});
    } else if (op == "scene_file") { // the whole scene as the .scene JSON (all property values)
        SceneEditorState editor;
        editor.cameraPosition = m_ctx.camera.position;
        editor.cameraYaw = m_ctx.camera.yaw;
        editor.cameraPitch = m_ctx.camera.pitch;
        editor.hasCamera = true;
        std::string text, error;
        if (!SerializeScene(m_ctx.scene, editor, text, error)) return fail(error);
        ok({{"scene", Json::parse(text, nullptr, false)}});
    } else if (op == "actor") {
        std::string error;
        const uint64_t actor = ResolveActor(m_ctx.scene, req, error);
        if (!actor) return fail(error);
        ok({{"actor", ActorJson(m_ctx.scene, actor, true)}});
    } else if (op == "spawn") {
        SpawnKind kind;
        const std::string what = Lower(req.value("kind", std::string("empty")));
        if (!FindSpawnKind(what, kind)) {
            std::string words;
            for (int i = 0; i < static_cast<int>(SpawnKind::Count); ++i) words += (i ? ", " : "") + std::string(GetSpawnInfo(static_cast<SpawnKind>(i)).command);
            return fail("unknown kind '" + what + "'; kinds: " + words);
        }
        const uint64_t actor = SpawnActor(m_ctx, kind);
        if (!actor) return fail("the actor could not be created");
        Json props = req.value("properties", Json::object());
        if (!props.is_object()) props = Json::object();
        if (req.contains("name") && req["name"].is_string()) props["Name"] = {{"Name", req["name"]}};
        if (req.contains("position")) props["Transform"]["Position"] = req["position"];
        if (req.contains("rotation")) props["Transform"]["Rotation"] = req["rotation"];
        if (req.contains("scale")) props["Transform"]["Scale"] = req["scale"];
        actorWithProblems(actor, ApplyJsonProperties(m_ctx, actor, props));
    } else if (op == "set") {
        std::string error;
        const uint64_t actor = ResolveActor(m_ctx.scene, req, error);
        if (!actor) return fail(error);
        Json props = req.value("properties", Json::object());
        if (req.contains("component") && req.contains("property") && req.contains("value")) {
            props[req["component"].get<std::string>()][req["property"].get<std::string>()] = req["value"];
        }
        if (!props.is_object() || props.empty()) return fail("give 'component', 'property' and 'value', or 'properties': {\"Component\": {\"Property\": value}}");
        const std::vector<std::string> problems = ApplyJsonProperties(m_ctx, actor, props);
        if (!problems.empty() && problems.size() >= 1 && !req.value("partial_ok", true)) return fail(problems[0]);
        actorWithProblems(actor, problems);
    } else if (op == "add_component" || op == "remove_component") {
        std::string error;
        const uint64_t actor = ResolveActor(m_ctx.scene, req, error);
        if (!actor) return fail(error);
        const ComponentInfo* info = FindComponent(req.value("component", std::string()));
        if (!info || info->hidden || !info->removable) return fail("unknown or fixed component '" + req.value("component", std::string()) + "' (see the reflect op for the list)");
        const bool has = info->has(m_ctx.scene.Registry(), m_ctx.scene.Find(actor));
        std::vector<std::string> problems;
        if (op == "add_component") {
            if (!has) AddComponentCommand(m_ctx, actor, *info);
            if (req.contains("properties")) problems = ApplyJsonProperties(m_ctx, actor, Json{{info->name, req["properties"]}});
        } else if (has) {
            RemoveComponentCommand(m_ctx, actor, *info);
        }
        actorWithProblems(actor, problems);
    } else if (op == "delete") {
        std::string error;
        const uint64_t actor = ResolveActor(m_ctx.scene, req, error);
        if (!actor) return fail(error);
        const std::string name = m_ctx.scene.TryGet<NameComponent>(actor)->name;
        DeleteActor(m_ctx, actor);
        ok({{"deleted", name}});
    } else if (op == "duplicate") {
        std::string error;
        const uint64_t actor = ResolveActor(m_ctx.scene, req, error);
        if (!actor) return fail(error);
        const uint64_t copy = DuplicateActor(m_ctx, actor);
        if (!copy) return fail("the actor could not be duplicated");
        actorWithProblems(copy, {});
    } else if (op == "select") {
        std::string error;
        const uint64_t actor = ResolveActor(m_ctx.scene, req, error);
        if (!actor) return fail(error);
        m_ctx.selected = actor;
        ok({{"selected", IdText(actor)}});
    } else if (op == "screenshot") {
        const std::string target = req.value("target", std::string("viewport"));
        if (target != "viewport" && target != "window") return fail("'target' is viewport or window");
        std::string path = req.value("path", std::string());
        if (path.empty()) path = (m_ctx.projectRoot / "Saved" / "Screenshots" / std::format("mcp_{}.png", m_frame)).generic_string();
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::filesystem::remove(path, ec);
        RequestScreenshot(target, path);
        settle(1, false);
        m_controlPending.screenshot = true;
        m_controlPending.path = path;
    } else if (op == "quit") {
        m_forceQuit = req.value("force", false);
        settle(1, false);
        m_controlPending.quit = true;
    } else {
        fail("unknown op '" + op + "' (status, exec, wait, log, reflect, scene, scene_file, actor, spawn, set, add_component, remove_component, delete, duplicate, select, screenshot, quit)");
    }
}

} // namespace magnum::editor
