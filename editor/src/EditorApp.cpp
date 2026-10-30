#include "EditorApp.h"

#include "Icons.h"
#include "Picking.h"
#include "Theme.h"
#include "commands/EditorCommands.h"
#include "commands/MaterialCommands.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "panels/PanelUtil.h"
#include "platform/FileOps.h"
#include "project/Project.h"
#include "render/Screenshot.h"
#include "scene/Reflection.h"
#include "scene/SceneSerializer.h"

#include <glad/gl.h>

#include <GLFW/glfw3.h>
#include <ImGuizmo.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h> // DockBuilder API

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace magnum::editor {

namespace fs = std::filesystem;

namespace {

std::string g_savedDir; // <project>/Saved, set once the project is known

// The projects opened before, newest first, kept next to the engine.
fs::path RecentFile() { return fs::path(MAGNUM_ENGINE_DIR) / "Saved" / "recent_projects.txt"; }

std::vector<fs::path> LoadRecentProjects() {
    std::vector<fs::path> out;
    std::ifstream in(RecentFile());
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::error_code ec;
        if (!line.empty() && fs::is_directory(line, ec)) out.emplace_back(line);
    }
    return out;
}

void SaveRecentProjects(const std::vector<fs::path>& list) {
    std::error_code ec;
    fs::create_directories(RecentFile().parent_path(), ec);
    std::ofstream out(RecentFile(), std::ios::trunc);
    for (const fs::path& p : list) out << p.generic_string() << '\n';
}

std::string TimestampedShotPath(const std::string& target) {
    using namespace std::chrono;
    const auto now = zoned_time{current_zone(), floor<seconds>(system_clock::now())};
    return std::format("{}/Screenshots/{}_{:%Y%m%d_%H%M%S}.png", g_savedDir, target, now);
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Reads the value for `prop` from the rest of a console line.
bool ParsePropertyValue(const PropertyInfo& prop, std::istringstream& in, PropertyValue& out) {
    switch (prop.kind) {
    case PropKind::Bool: {
        std::string t;
        if (!(in >> t)) return false;
        out = (t == "1" || Lower(t) == "true");
        return true;
    }
    case PropKind::Int: {
        int i;
        if (!(in >> i)) return false;
        out = i;
        return true;
    }
    case PropKind::Float: {
        float f;
        if (!(in >> f)) return false;
        out = f;
        return true;
    }
    case PropKind::Vec3:
    case PropKind::Color3: {
        glm::vec3 v;
        if (!(in >> v.x >> v.y >> v.z)) return false;
        out = v;
        return true;
    }
    case PropKind::String:
    case PropKind::CString: {
        std::string rest;
        std::getline(in, rest);
        out = Trim(rest);
        return true;
    }
    case PropKind::Enum: {
        std::string t;
        if (!(in >> t)) return false;
        for (size_t i = 0; i < prop.options.size(); ++i) {
            if (Lower(t) == Lower(prop.options[i])) {
                out = static_cast<int>(i);
                return true;
            }
        }
        return false;
    }
    }
    return false;
}

// "Content/Scenes/Name.scene" from whatever the user typed in Save As.
std::string NormalizeScenePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    path = Trim(path);
    if (path.empty()) return {};
    if (path.rfind("Content/", 0) != 0) path = "Content/Scenes/" + path;
    if (Lower(path).rfind(".scene") != path.size() - 6 || path.size() < 7) path += ".scene";
    return path;
}

} // namespace

bool EditorApp::Init(const EditorOptions& options) {
    m_options = options;

    // Which project: --project, else the last one (not for scripted runs, which stay reproducible), else the sample.
    const fs::path engineRoot = MAGNUM_ENGINE_DIR;
    SetEngineRoot(engineRoot);
    m_recentProjects = LoadRecentProjects();
    fs::path project = engineRoot / "Projects" / "MagnumDemo";
    if (!options.project.empty()) project = options.project;
    else if (options.exec.empty() && options.shotPath.empty() && !m_recentProjects.empty()) project = m_recentProjects.front();
    {
        std::error_code ec;
        project = fs::weakly_canonical(fs::absolute(project), ec);
        // A missing or empty folder becomes a fresh project (starter content, pipeline, game module).
        ProjectInfo probe;
        if (!LoadProjectInfo(project, probe) && (!fs::exists(project, ec) || fs::is_empty(project, ec))) {
            std::string error;
            if (!CreateProject(project, project.filename().string(), engineRoot / "Engine" / "Templates" / "Project", error)) {
                std::fprintf(stderr, "Could not create the project: %s\n", error.c_str());
            }
        }
        fs::create_directories(project, ec);
    }
    m_ctx.projectRoot = project;
    SetProjectRoot(project);
    m_uiPreview.Open(project);
    m_ctx.buildUiDraw = [this](int width, int height, UiDrawList& out) {
        UiSystem& ui = m_ctx.playing ? m_gameplay.Ui() : m_uiPreview.System();
        ui.SetViewSize(width, height);
        ui.Layout(); // a resized viewport lays out again before it is drawn
        ui.BuildDrawList(out);
    };
    g_savedDir = (project / "Saved").generic_string();
    Log::Init(g_savedDir + "/Logs/magnum.log");
    Log::Info("Magnum Editor starting");
    if (options.controlPort >= 0) {
        std::string controlError;
        if (!m_control.Start(options.controlPort, project / "Saved" / "control.json", project.generic_string(), controlError)) {
            Log::Error("Control channel: {}", controlError);
        }
    }

    if (!m_window.Create({})) {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable |
                      ImGuiConfigFlags_NavEnableKeyboard;
    // The docked layout is remembered between runs.
    fs::create_directories(g_savedDir);
    m_imguiIniPath = g_savedDir + "/editor_layout.ini";
    io.IniFilename = m_imguiIniPath.c_str();

    LoadFonts(14.0f);

    ApplyTheme();
    m_graphPanel.Init(); // ImNodes needs the ImGui context that was just created
    ImGui_ImplGlfw_InitForOpenGL(m_window.Handle(), true);
    ImGui_ImplOpenGL3_Init("#version 450 core");

    m_ctx.window = &m_window;
    {   // the .magnum file names the project and its default scene; a plain folder becomes a project
        ProjectInfo info;
        if (!LoadProjectInfo(m_ctx.projectRoot, info)) {
            std::string error;
            info.name = m_ctx.projectRoot.filename().string();
            std::ofstream(m_ctx.projectRoot / (info.name + ".magnum")) << "{\n  \"magnumProject\": 1,\n  \"name\": \"" << info.name
                                                                     << "\",\n  \"defaultScene\": \"" << m_defaultScene << "\"\n}\n";
            Log::Info("{} had no .magnum file; created one", m_ctx.projectRoot.string());
        }
        m_projectName = info.name;
        m_defaultScene = info.defaultScene;
        m_recentProjects.erase(std::remove(m_recentProjects.begin(), m_recentProjects.end(), m_ctx.projectRoot), m_recentProjects.end());
        m_recentProjects.insert(m_recentProjects.begin(), m_ctx.projectRoot);
        if (m_recentProjects.size() > 8) m_recentProjects.resize(8);
        SaveRecentProjects(m_recentProjects);
        Log::Info("Project '{}' ({})", m_projectName, m_ctx.projectRoot.string());
    }
    m_ctx.shaders.Init(m_ctx.projectRoot, "Engine/Lua/shader_dsl.lua");
    m_ctx.materials.Init(m_ctx.projectRoot);
    m_ctx.pipeline.Init(m_ctx.projectRoot, "Content/Pipelines/default.lua");
    m_ctx.textures.Init(m_ctx.projectRoot);
    m_ctx.renderer.Init();
    m_ctx.openScene = [this](const std::string& path) { RequestAction(PendingAction::OpenScene, path); };

    PluginServices services;
    services.exec = [this](const std::string& line) { ExecuteCommand(line); };
    services.selectedName = [this]() -> std::string {
        const NameComponent* n = m_ctx.scene.TryGet<NameComponent>(m_ctx.selected);
        return n ? n->name : std::string();
    };
    services.actorNames = [this]() {
        std::vector<std::string> names;
        for (const uint64_t id : m_ctx.scene.Order()) names.push_back(m_ctx.scene.TryGet<NameComponent>(id)->name);
        return names;
    };
    services.mcpStatus = [this]() { return McpStatus(); };
    services.mcpInfo = [this]() { return McpInfoNow(); };
    m_plugins.Init(std::filesystem::path(m_ctx.projectRoot) / "Plugins", std::move(services));

    // Game module: load the last build now (so scene files can bind their game components), and let the builder
    // rebuild it whenever Game/ changes.
    const fs::path root = m_ctx.projectRoot;
    m_gameDll = root / "Saved" / "GameBuild" / "bin" / "MagnumGame.dll";
    m_game.SetModuleDirectory(g_savedDir + "/GameModules");
    m_game.keyDown = [this](int key) {
        return m_ctx.gameInputEnabled && glfwGetKey(m_window.Handle(), key) == GLFW_PRESS;
    };
    if (fs::exists(m_gameDll)) ReloadGameModule();
    GameBuilder::Config build;
    build.projectRoot = root;
    build.script = engineRoot / "scripts" / "build_game.bat";
    build.dll = m_gameDll;
    build.logFile = fs::path(g_savedDir) / "Logs" / "game_build.log";
    build.watch = {root / "Game" / "src", root / "Game" / "CMakeLists.txt", engineRoot / "engine" / "include" / "magnum_game_api.h"};
    m_builder.Init(build);

    // The project's default level; created on first run so there is always a file to open.
    if (!fs::exists(m_ctx.projectRoot / m_defaultScene) || !OpenSceneFile(m_defaultScene)) {
        BuildDefaultScene();
        SaveSceneAs(m_defaultScene);
    }

    // --exec "a; b; wait 60; c": the commands run one after another from the third frame on.
    {
        std::istringstream commands(m_options.exec);
        std::string line;
        while (std::getline(commands, line, ';')) {
            line = Trim(line);
            if (!line.empty()) m_script.push_back(line);
        }
    }

    m_console.onCommand = [this](const std::string& line) { ExecuteCommand(line); };
    Log::Info("Ready. Type 'help' in the console for commands.");
    return true;
}

void EditorApp::BuildDefaultScene() {
    Scene& scene = m_ctx.scene;
    scene.Clear();
    entt::registry& reg = scene.Registry();

    auto make = [&](const char* name, glm::vec3 position) {
        const uint64_t id = scene.CreateActor(name);
        reg.get<TransformComponent>(scene.Find(id)).position = position;
        return scene.Find(id);
    };

    entt::entity e = make("Cube", {0.0f, 0.5f, 0.0f});
    MeshComponent cube;
    cube.material = "Content/Materials/Default.lua";
    reg.emplace<MeshComponent>(e, cube);

    e = make("Sphere", {2.0f, 0.5f, -0.6f});
    MeshComponent sphere;
    sphere.primitive = Primitive::Sphere;
    sphere.material = "Content/Materials/Default.lua";
    reg.emplace<MeshComponent>(e, sphere);

    e = make("Red Sphere", {-1.8f, 0.5f, 1.2f});
    MeshComponent red;
    red.primitive = Primitive::Sphere;
    red.material = "Content/Materials/Default.lua";
    reg.emplace<MeshComponent>(e, red);

    e = make("Directional Light", {0.0f, 4.0f, 0.0f});
    reg.get<TransformComponent>(e).rotation = {-50.0f, 30.0f, 0.0f};
    reg.emplace<LightComponent>(e);

    m_ctx.camera = EditorCamera{};
    m_ctx.camera.position = {5.5f, 3.5f, 7.0f};
    m_ctx.camera.LookAt({0.7f, 0.5f, 0.0f});
    m_ctx.selected = 0;
}

// ---- Game module and play-in-editor ------------------------------------------------------------

void EditorApp::ReloadGameModule() {
    const bool wasLoaded = m_game.IsLoaded();
    std::string error;
    if (!m_game.LoadModule(m_gameDll, m_ctx.scene, error)) {
        Log::Error("Game module was not {}: {}{}", wasLoaded ? "reloaded" : "loaded", error,
                   wasLoaded ? " (the previous version keeps running)" : "");
        return;
    }
    std::string names;
    for (const std::string& n : m_game.ComponentNames()) names += (names.empty() ? "" : ", ") + n;
    Log::Info("Game module {} (version {}): {}", wasLoaded ? "reloaded" : "loaded", m_game.Generation(),
              names.empty() ? "no components" : names);
}

void EditorApp::UpdateGame() {
    m_builder.Poll();
    if (m_builder.TakeSuccessfulBuild()) ReloadGameModule();

    m_ctx.gameStatus = m_builder.Status();
    if (m_ctx.gameStatus == "Game: ready" && !m_game.IsLoaded()) m_ctx.gameStatus = "Game: not loaded";

    // Play/Stop requested by a panel last frame; handled here, between frames, so nothing holds stale pointers.
    const EditorContext::PlayRequest request = m_ctx.playRequest;
    m_ctx.playRequest = EditorContext::PlayRequest::None;
    if (request == EditorContext::PlayRequest::Play && !m_ctx.playing) StartPlay();
    else if (request == EditorContext::PlayRequest::Stop && m_ctx.playing) StopPlay();

    if (m_ctx.playing && !m_ctx.paused) {
        m_game.Update(m_ctx.scene, m_ctx.deltaTime);
        if (m_game.Faulted()) {
            Log::Error("Play stopped: {}. Fix the code; the module reloads when the build succeeds.", m_game.FaultMessage());
            StopPlay();
        }
    }
    if (m_ctx.playing && !m_ctx.paused) {
        GameInput input;
        GLFWwindow* window = m_window.Handle();
        input.keyDown = [this, window](int key) { return (m_ctx.gameInputEnabled && glfwGetKey(window, key) == GLFW_PRESS) || m_scriptKeys.contains(key); };
        input.mouseDown = [this](int button) { return (m_ctx.gameInputEnabled || m_scriptMouse.active) && ImGui::IsMouseDown(button); }; // ImGui reports the real buttons too, and scripted runs can press them
        input.mouseDelta = m_ctx.mouseCaptured ? m_ctx.gameMouseDelta : glm::vec2(0.0f);
        input.mousePosition = m_ctx.gameMousePosition;
        input.mouseInView = m_ctx.gameMouseInView && !m_ctx.mouseCaptured;
        input.aspect = m_ctx.gameAspect;
        input.viewSize = m_ctx.gameViewSize;
        input.mouseWheel = m_ctx.gameMouseWheel;
        m_gameplay.Update(m_ctx.scene, m_ctx.deltaTime, input);
        for (auto it = m_scriptKeys.begin(); it != m_scriptKeys.end();) { // taps wear off
            if (it->second > 0 && --it->second == 0) it = m_scriptKeys.erase(it);
            else ++it;
        }
        m_ctx.uiPointerOver = m_gameplay.Ui().PointerOverUi() || m_gameplay.Ui().PressedNode() != 0;
        if (m_gameplay.StopRequested()) {
            Log::Info("A script asked to stop the game");
            StopPlay();
        }
    }
    m_ctx.activeUi = m_ctx.playing ? &m_gameplay.Ui() : &m_uiPreview.System();
    if (!m_ctx.playing) {
        m_ctx.uiPointerOver = false;
        m_uiPreview.Update(m_ctx.scene, m_ctx.deltaTime, static_cast<int>(m_ctx.gameViewSize.x), static_cast<int>(m_ctx.gameViewSize.y));
    }
    if (m_ctx.playing) { // what the player sees and what scripts drew (also while paused)
        m_ctx.gameCamera = Gameplay::FindCamera(m_ctx.scene);
        m_ctx.hud = m_gameplay.Hud();
    }
}

void EditorApp::StartPlay() {
    if (m_ctx.playing) return;
    SceneEditorState editor; // camera is not part of play
    std::string error;
    if (!SerializeScene(m_ctx.scene, editor, m_playBackup, error)) {
        Log::Error("Cannot start play: {}", error);
        return;
    }
    if (m_builder.Pending()) Log::Info("The game is still building; code changes join the running session when the build finishes");

    m_ctx.commands.BeginSession();
    m_ctx.playing = true;
    if (!m_game.BeginPlay(m_ctx.scene)) {
        Log::Error("Play stopped: {}", m_game.FaultMessage());
        StopPlay();
        return;
    }
    m_gameplay.Begin(m_ctx.scene, m_ctx.projectRoot);
    m_ctx.gameCamera = Gameplay::FindCamera(m_ctx.scene);
    if (m_ctx.gameCamera.valid) Log::Info("Playing through the scene camera: click the viewport to capture the mouse, Esc releases it");
    Log::Info("Play started{}", m_game.IsLoaded() ? "" : " (no game module loaded: only the scene runs)");
}

void EditorApp::StopPlay() {
    m_ctx.paused = false;
    if (!m_ctx.playing) return;
    m_gameplay.End();
    m_game.EndPlay(m_ctx.scene);
    m_ctx.hud.clear();
    m_ctx.gameCamera = CameraView{};
    if (m_ctx.mouseCaptured) {
        m_ctx.mouseCaptured = false;
        glfwSetInputMode(m_window.Handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    // Everything that happened while playing is thrown away: the scene comes back exactly as it was.
    SceneEditorState editor;
    std::string error;
    if (!DeserializeScene(m_ctx.scene, editor, m_playBackup, error)) {
        Log::Error("Could not restore the scene after play: {}", error);
    }
    m_game.ResolvePending(m_ctx.scene);
    m_ctx.commands.EndSession();
    m_ctx.playing = false;
    m_ctx.gameInputEnabled = false;
    m_playBackup.clear();
    Log::Info("Play stopped, scene restored");
}

// ---- Scene files -------------------------------------------------------------------------------

void EditorApp::NewScene() {
    m_ctx.scene.Clear();
    m_ctx.selected = 0;
    m_ctx.commands.Clear();
    m_ctx.scenePath.clear();
    m_ctx.savedRevision = m_ctx.commands.Revision();
    Log::Info("New scene");
}

bool EditorApp::OpenSceneFile(const std::string& relPath) {
    SceneEditorState editor;
    std::string error;
    if (!LoadScene(m_ctx.scene, editor, m_ctx.projectRoot / relPath, error)) {
        Log::Error("Cannot open {}: {}", relPath, error);
        return false;
    }
    m_game.ResolvePending(m_ctx.scene); // binds game components saved in the file to the loaded module's types
    m_ctx.selected = 0;
    m_ctx.commands.Clear();
    m_ctx.scenePath = relPath;
    m_ctx.savedRevision = m_ctx.commands.Revision();
    if (editor.hasCamera) {
        m_ctx.camera.position = editor.cameraPosition;
        m_ctx.camera.yaw = editor.cameraYaw;
        m_ctx.camera.pitch = editor.cameraPitch;
    }
    Log::Info("Opened {} ({} actor(s))", relPath, m_ctx.scene.Order().size());
    return true;
}

bool EditorApp::SaveSceneAs(const std::string& relPath) {
    SceneEditorState editor;
    editor.hasCamera = true;
    editor.cameraPosition = m_ctx.camera.position;
    editor.cameraYaw = m_ctx.camera.yaw;
    editor.cameraPitch = m_ctx.camera.pitch;

    std::string error;
    if (!SaveScene(m_ctx.scene, editor, m_ctx.projectRoot / relPath, error)) {
        Log::Error("Cannot save {}: {}", relPath, error);
        return false;
    }
    m_ctx.scenePath = relPath;
    m_ctx.savedRevision = m_ctx.commands.Revision();
    Log::Info("Saved {}", relPath);
    return true;
}

bool EditorApp::SaveSceneFile() {
    if (m_ctx.playing) {
        Log::Warn("Stop playing before saving: the running scene is thrown away on Stop");
        return false;
    }
    if (m_ctx.scenePath.empty()) {
        m_openSaveAsPopup = true; // untitled: ask for a name first
        return false;
    }
    return SaveSceneAs(m_ctx.scenePath);
}

void EditorApp::RequestAction(PendingAction action, const std::string& path) {
    if (m_ctx.playing && action != PendingAction::Quit) {
        Log::Warn("Stop playing before opening or creating a scene");
        return;
    }
    m_pending = action;
    m_pendingPath = path;
    if (m_ctx.IsDirty()) m_openUnsavedPopup = true;
    else PerformPending();
}

void EditorApp::PerformPending() {
    const PendingAction action = m_pending;
    const std::string path = m_pendingPath;
    m_pending = PendingAction::None;
    switch (action) {
    case PendingAction::NewScene: NewScene(); break;
    case PendingAction::OpenScene: OpenSceneFile(path); break;
    case PendingAction::Quit:
        m_forceQuit = true;
        m_window.RequestClose();
        break;
    case PendingAction::SwitchProject:
        if (LaunchSelf(L"--project \"" + fs::path(path).wstring() + L"\"")) {
            m_forceQuit = true;
            m_window.RequestClose();
        } else {
            Log::Error("Could not start the editor for {}", path);
        }
        break;
    case PendingAction::None: break;
    }
}

void EditorApp::RequestOpenProject(const fs::path& folder) {
    if (fs::weakly_canonical(folder) == m_ctx.projectRoot) {
        Log::Info("{} is already open", m_projectName);
        return;
    }
    RequestAction(PendingAction::SwitchProject, folder.string());
}

// File > New Project: pick a folder, the project is created in it and opened in a new editor window.
void EditorApp::NewProjectDialog() {
    const fs::path folder = PickFolder(L"Choose a folder for the new project (empty or not created yet)", m_ctx.projectRoot.parent_path());
    if (folder.empty()) return;
    std::string error;
    if (!CreateProject(folder, folder.filename().string(), fs::path(MAGNUM_ENGINE_DIR) / "Engine" / "Templates" / "Project", error)) {
        Log::Error("New project: {}", error);
        return;
    }
    Log::Info("Created project '{}' in {}", folder.filename().string(), folder.string());
    RequestOpenProject(folder);
}

// File > Open Project: pick the project folder (the one with the .magnum file).
void EditorApp::OpenProjectDialog() {
    const fs::path folder = PickFolder(L"Open a project folder (it contains a .magnum file)", m_ctx.projectRoot.parent_path());
    if (folder.empty()) return;
    ProjectInfo info;
    if (!LoadProjectInfo(folder, info)) {
        Log::Error("{} has no .magnum file, so it is not a Magnum project", folder.string());
        return;
    }
    RequestOpenProject(folder);
}

void EditorApp::DrawSceneDialogs() {
    if (m_openUnsavedPopup) {
        ImGui::OpenPopup("Unsaved Changes");
        m_openUnsavedPopup = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string name = m_ctx.scenePath.empty() ? "Untitled" : m_ctx.scenePath;
        ImGui::Text("Save changes to '%s' first?", name.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(100, 0))) {
            if (m_ctx.scenePath.empty()) {
                m_runPendingAfterSave = true;
                m_openSaveAsPopup = true;
                ImGui::CloseCurrentPopup();
            } else if (SaveSceneFile()) {
                PerformPending();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(100, 0))) {
            PerformPending();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_pending = PendingAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_openSaveAsPopup) {
        ImGui::OpenPopup("Save Scene As");
        std::snprintf(m_saveAsBuffer, sizeof(m_saveAsBuffer), "%s",
                      m_ctx.scenePath.empty() ? "Content/Scenes/Untitled.scene" : m_ctx.scenePath.c_str());
        m_openSaveAsPopup = false;
    }
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(380.0f);
        const bool enter = ImGui::InputText("##path", m_saveAsBuffer, sizeof(m_saveAsBuffer),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Save", ImVec2(100, 0)) || enter) {
            const std::string path = NormalizeScenePath(m_saveAsBuffer);
            if (!path.empty() && SaveSceneAs(path)) {
                if (m_runPendingAfterSave) PerformPending();
                m_runPendingAfterSave = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_runPendingAfterSave = false;
            m_pending = PendingAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::UpdateWindowTitle() {
    const std::string name = m_ctx.scenePath.empty() ? "Untitled" : fs::path(m_ctx.scenePath).filename().string();
    const std::string title = "Magnum Editor - " + name + (m_ctx.IsDirty() ? "*" : "");
    if (title != m_windowTitle) {
        m_windowTitle = title;
        m_window.SetTitle(title);
    }
}

// ---- Frame loop --------------------------------------------------------------------------------

void EditorApp::Run() {
    // An editor that is not in front (or minimized) must not take the GPU from the other programs: a browser playing
    // video next to it would stutter. Scripted runs are never throttled.
    const bool scripted = !m_options.exec.empty() || !m_options.shotPath.empty() || m_control.Running(); // a driven editor must not slow down in the background
    for (;;) {
        const auto frameStart = std::chrono::steady_clock::now();
        m_window.PollEvents();
        if (m_window.ShouldClose()) {
            if (m_ctx.playing) StopPlay(); // restores the scene first, so playing never counts as unsaved changes
            if (m_ctx.IsDirty() && !m_forceQuit) {
                m_window.CancelClose(); // closing with unsaved changes asks first
                if (m_pending != PendingAction::Quit) RequestAction(PendingAction::Quit);
            } else {
                break;
            }
        }

        if (!scripted && glfwGetWindowAttrib(m_window.Handle(), GLFW_ICONIFIED)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            continue;
        }

        m_ctx.deltaTime = ImGui::GetIO().DeltaTime;
        m_ctx.time = glfwGetTime();
        m_ctx.shaders.Poll();
        m_ctx.materials.Poll();
        m_ctx.pipeline.Poll();
        m_ctx.textures.Poll();
        m_ctx.thumbnails.BeginFrame();
        UpdateGame();
        m_plugins.Update(m_ctx.deltaTime);
        PumpScript();
        PumpControl();
        if (!m_ctx.scene.Exists(m_ctx.selected)) m_ctx.selected = 0;
        if (m_ctx.selected != m_lastSelected) { // picking an actor goes back to showing its own material
            if (m_ctx.selected != 0) m_ctx.inspectedMaterial.clear();
            m_lastSelected = m_ctx.selected;
        }

        BeginFrame();
        DrawMainMenu();
        DrawMainToolbar();
        DrawStatusBar();
        DrawDockHost();
        HandleShortcuts();
        m_placeActors.Draw(m_ctx);
        m_outliner.Draw(m_ctx);
        m_details.Draw(m_ctx);
        m_uiPanel.Draw(m_ctx);
        m_materialPanel.Draw(m_ctx);
        m_contentBrowser.Draw(m_ctx);
        m_console.Draw("Console");
        m_viewport.Draw(m_ctx);
        m_graphPanel.Draw(m_ctx);
        m_plugins.DrawPanels();
        if (m_focusViewport) { // after a (re)built layout the viewport, not the graph editor, is the visible tab
            ImGui::SetWindowFocus("Viewport");
            m_focusViewport = false;
        }
        DrawSceneDialogs();
        ShowIconSheet(&m_showIconSheet);
        UpdateWindowTitle();
        EndFrame();
        ++m_frame;

        if (!scripted && !glfwGetWindowAttrib(m_window.Handle(), GLFW_FOCUSED) && !m_ctx.playing) {
            constexpr auto kIdleFrame = std::chrono::milliseconds(50); // 20 FPS in the background
            const auto spent = std::chrono::steady_clock::now() - frameStart;
            if (spent < kIdleFrame) std::this_thread::sleep_for(kIdleFrame - spent);
        }
    }
}

void EditorApp::Shutdown() {
    m_control.Stop();
    if (m_ctx.playing) StopPlay();
    m_game.UnloadModule(m_ctx.scene);
    m_viewport.Shutdown();
    m_ctx.ShutdownGraphics(); // while the GL context still exists
    m_graphPanel.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_window.Destroy();
    Log::Info("Magnum Editor stopped");
    Log::Shutdown();
}

void EditorApp::BeginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    if (m_scriptMouse.active) { // a scripted pointer: the real cursor must not take the mouse back every frame
        ImGuiIO& scripted = ImGui::GetIO();
        scripted.AddMousePosEvent(m_scriptMouse.x, m_scriptMouse.y);
        scripted.AddMouseButtonEvent(0, m_scriptMouse.down);
    }
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void EditorApp::EndFrame() {
    ImGui::Render();

    int w = 0, h = 0;
    m_window.FramebufferSize(w, h);
    Framebuffer::BindDefault();
    glViewport(0, 0, w, h);
    glClearColor(0.055f, 0.055f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Window capture must happen before extra OS windows swap and before our own swap.
    ProcessScreenshot();

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* mainContext = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(mainContext);
    }
    m_window.SwapBuffers();
}

void EditorApp::HandleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
        m_ctx.playRequest = m_ctx.playing ? EditorContext::PlayRequest::Stop : EditorContext::PlayRequest::Play;
    }
    if (m_ctx.playing && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (m_ctx.mouseCaptured) { // the first Esc gives the mouse back, the second one stops
            m_ctx.mouseCaptured = false;
            glfwSetInputMode(m_window.Handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else {
            m_ctx.playRequest = EditorContext::PlayRequest::Stop;
        }
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (io.KeyShift) m_ctx.commands.Redo();
        else m_ctx.commands.Undo();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) m_ctx.commands.Redo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (io.KeyShift) m_openSaveAsPopup = true;
        else SaveSceneFile();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) RequestAction(PendingAction::NewScene);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) DuplicateActor(m_ctx, m_ctx.selected);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) DeleteActor(m_ctx, m_ctx.selected);
}

void EditorApp::DrawDockHost() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockId = ImGui::GetID("MagnumDockSpace");
    if (m_resetLayout || ImGui::DockBuilderGetNode(dockId) == nullptr) {
        BuildDefaultLayout(dockId, vp->WorkSize.x, vp->WorkSize.y);
        m_resetLayout = false;
    }
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();
}

namespace {

// The Magnum mark: an arch (magnum door) in the accent color with a dark opening.
void DrawLogo(ImDrawList* dl, ImVec2 c, float size) {
    const float r = size * 0.35f;
    dl->PathArcTo(ImVec2(c.x, c.y - size * 0.1f), r, 3.14159265f, 6.2831853f, 16);
    dl->PathLineTo(ImVec2(c.x + r, c.y + size * 0.4f));
    dl->PathLineTo(ImVec2(c.x - r, c.y + size * 0.4f));
    dl->PathFillConvex(ui::U32(ui::kAccent));
    const float ri = size * 0.15f;
    dl->PathArcTo(ImVec2(c.x, c.y + size * 0.1f), ri, 3.14159265f, 6.2831853f, 12);
    dl->PathLineTo(ImVec2(c.x + ri, c.y + size * 0.4f));
    dl->PathLineTo(ImVec2(c.x - ri, c.y + size * 0.4f));
    dl->PathFillConvex(ui::U32(ui::kBase));
}

// A toolbar button with a border: `active` colors it with the accent.
bool ToolbarButton(const char* label, bool active = false) {
    ImGui::PushID(label);
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 size(text.x + 20.0f, 28.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##tb", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, end, ui::U32(active ? ui::kSelection : (hovered ? ui::kHover : ui::kPanel)), 5.0f);
    dl->AddRect(pos, end, ui::U32(active ? ui::kAccent : ui::kBorder), 5.0f);
    dl->AddText(ImVec2(pos.x + 10.0f, pos.y + (size.y - text.y) * 0.5f), ui::U32(active ? ui::kText : ui::kTextSoft), label);
    ImGui::PopID();
    return clicked;
}

// A pill with a status dot ("C++ built").
float DrawChip(ImVec2 pos, const char* label, const ImVec4& dot) {
    const ImVec2 text = ImGui::CalcTextSize(label);
    const float width = text.x + 34.0f, height = 24.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), ui::U32(ui::kBorder), 12.0f);
    dl->AddCircleFilled(ImVec2(pos.x + 14.0f, pos.y + height * 0.5f), 3.5f, ui::U32(dot));
    dl->AddText(ImVec2(pos.x + 24.0f, pos.y + (height - text.y) * 0.5f), ui::U32(ui::kTextSoft), label);
    return width;
}

float ChipWidth(const char* label) {
    return ImGui::CalcTextSize(label).x + 34.0f;
}

} // namespace

void EditorApp::DrawMainMenu() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 7.0f));
    const bool menuOpen = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();
    if (menuOpen) {
        {   // logo and name
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetFrameHeight();
            DrawLogo(ImGui::GetWindowDrawList(), ImVec2(p.x + 12.0f, p.y + h * 0.5f), 20.0f);
            ImGui::Dummy(ImVec2(24.0f, h));
            ImGui::SameLine(0, 0);
            ImGui::PushFont(GetFont(UiFont::Semibold));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Magnum");
            ImGui::PopFont();
            ImGui::SameLine(0, 14.0f);
        }
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) RequestAction(PendingAction::NewScene);
            if (ImGui::BeginMenu("Open Scene")) {
                std::error_code ec;
                bool any = false;
                for (const auto& item : fs::recursive_directory_iterator(m_ctx.projectRoot / "Content", ec)) {
                    if (item.path().extension() != ".scene") continue;
                    any = true;
                    const std::string rel = fs::relative(item.path(), m_ctx.projectRoot).generic_string();
                    if (ImGui::MenuItem(rel.c_str(), nullptr, rel == m_ctx.scenePath)) {
                        RequestAction(PendingAction::OpenScene, rel);
                    }
                }
                if (!any) ImGui::TextDisabled("No scenes in Content/");
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("New Project...")) NewProjectDialog();
            if (ImGui::MenuItem("Open Project...")) OpenProjectDialog();
            if (ImGui::BeginMenu("Recent Projects", !m_recentProjects.empty())) {
                for (const fs::path& recent : m_recentProjects) {
                    if (ImGui::MenuItem(recent.filename().string().c_str(), recent.generic_string().c_str(), recent == m_ctx.projectRoot)) {
                        RequestOpenProject(recent);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) SaveSceneFile();
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) m_openSaveAsPopup = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) m_window.RequestClose();
            m_plugins.DrawMenuItems("File");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            const std::string undo = m_ctx.commands.CanUndo() ? "Undo " + m_ctx.commands.UndoName() : "Undo";
            const std::string redo = m_ctx.commands.CanRedo() ? "Redo " + m_ctx.commands.RedoName() : "Redo";
            if (ImGui::MenuItem(undo.c_str(), "Ctrl+Z", false, m_ctx.commands.CanUndo())) m_ctx.commands.Undo();
            if (ImGui::MenuItem(redo.c_str(), "Ctrl+Y", false, m_ctx.commands.CanRedo())) m_ctx.commands.Redo();
            ImGui::Separator();
            const bool hasSelection = m_ctx.selected != 0;
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection)) DuplicateActor(m_ctx, m_ctx.selected);
            if (ImGui::MenuItem("Delete", "Del", false, hasSelection)) DeleteActor(m_ctx, m_ctx.selected);
            m_plugins.DrawMenuItems("Edit");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Game")) {
            if (!m_ctx.playing && ImGui::MenuItem("Play", "Alt+P")) m_ctx.playRequest = EditorContext::PlayRequest::Play;
            if (m_ctx.playing && ImGui::MenuItem("Stop", "Esc")) m_ctx.playRequest = EditorContext::PlayRequest::Stop;
            ImGui::Separator();
            if (ImGui::MenuItem("Rebuild Game")) m_builder.RequestBuild();
            if (ImGui::MenuItem("Reload Game Module")) ReloadGameModule();
            ImGui::Separator();
            ImGui::TextDisabled("%s", m_ctx.gameStatus.c_str());
            m_plugins.DrawMenuItems("Game");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Reset Layout")) m_resetLayout = true;
            m_plugins.DrawMenuItems("Window");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Screenshot Viewport")) RequestScreenshot("viewport", "");
            if (ImGui::MenuItem("Screenshot Window")) RequestScreenshot("window", "");
            ImGui::Separator();
            ImGui::MenuItem("Icon Sheet", nullptr, &m_showIconSheet);
            m_plugins.DrawMenuItems("Tools");
            ImGui::Separator();
            m_plugins.DrawPluginsMenu();
            ImGui::EndMenu();
        }
        for (const std::string& extra : m_plugins.ExtraMenus({"File", "Edit", "Game", "Window", "Tools"})) {
            if (ImGui::BeginMenu(extra.c_str())) {
                m_plugins.DrawMenuItems(extra);
                ImGui::EndMenu();
            }
        }
        {   // project / scene in the middle, version on the right
            const std::string scene = m_ctx.scenePath.empty() ? "Untitled" : m_ctx.scenePath;
            const std::string title = m_projectName + "  /  " + scene;
            const float width = ImGui::GetWindowWidth();
            ImGui::SetCursorPosX((width - ImGui::CalcTextSize(title.c_str()).x) * 0.5f);
            ImGui::TextDisabled("%s", title.c_str());
            ImGui::PushFont(GetFont(UiFont::Mono));
            const char* version = "Magnum 0.1";
            ImGui::SameLine(width - ImGui::CalcTextSize(version).x - 14.0f);
            ImGui::TextDisabled("%s", version);
            ImGui::PopFont();
        }
        ImGui::EndMainMenuBar();
    }
}

// The bar under the menu: viewport tools on the left, Play/Pause/Stop in the middle, build state on the right.
void EditorApp::DrawMainToolbar() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::kPanel);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginViewportSideBar("##MainToolbar", vp, ImGuiDir_Up, 44.0f, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const float winWidth = ImGui::GetWindowWidth();
        dl->AddLine(ImVec2(winPos.x, winPos.y + 43.5f), ImVec2(winPos.x + winWidth, winPos.y + 43.5f), ui::U32(ui::kBorder));

        const float top = (44.0f - 28.0f) * 0.5f;
        ImGui::SetCursorPosY(top);

        // Tool group: one of the four is always on.
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(ImVec2(p.x, p.y - 2), ImVec2(p.x + 4 * 28 + 3 * 2 + 4, p.y + 30), ui::U32(ui::kBg), 7.0f);
            dl->AddRect(ImVec2(p.x, p.y - 2), ImVec2(p.x + 4 * 28 + 3 * 2 + 4, p.y + 30), ui::U32(ui::kBorder), 7.0f);
            ImGui::SetCursorScreenPos(ImVec2(p.x + 2, p.y));
            struct Tool { const char* id; Icon icon; const char* tip; GizmoMode mode; };
            const Tool tools[] = {{"##select", Icon::Select, "Select (Q)", GizmoMode::Select},
                                  {"##move", Icon::Move, "Move (W)", GizmoMode::Translate},
                                  {"##rotate", Icon::Rotate, "Rotate (E)", GizmoMode::Rotate},
                                  {"##scale", Icon::Scale, "Scale (R)", GizmoMode::Scale}};
            for (size_t i = 0; i < 4; ++i) {
                if (i) ImGui::SameLine(0, 2);
                if (IconButton(tools[i].id, tools[i].icon, tools[i].tip, m_ctx.gizmoMode == tools[i].mode, 28)) m_ctx.gizmoMode = tools[i].mode;
            }
        }
        ImGui::SameLine(0, 12);
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddLine(ImVec2(p.x, p.y + 3), ImVec2(p.x, p.y + 25), ui::U32(ui::kBorder));
            ImGui::Dummy(ImVec2(1, 28));
        }
        ImGui::SameLine(0, 12);
        if (ToolbarButton(m_ctx.gizmoSnap ? "Grid snap  \xC2\xB7  On" : "Grid snap  \xC2\xB7  Off", m_ctx.gizmoSnap)) m_ctx.gizmoSnap = !m_ctx.gizmoSnap;
        ImGui::SameLine(0, 6);
        if (ToolbarButton(m_ctx.gizmoLocal ? "Local space" : "World space")) m_ctx.gizmoLocal = !m_ctx.gizmoLocal;
        ImGui::SameLine(0, 6);
        if (IconButton("##undo", Icon::Undo, "Undo (Ctrl+Z)", false, 28, m_ctx.commands.CanUndo() ? 0 : ui::U32(ui::kTextFaint)) && m_ctx.commands.CanUndo()) m_ctx.commands.Undo();
        ImGui::SameLine(0, 2);
        if (IconButton("##redo", Icon::Redo, "Redo (Ctrl+Y)", false, 28, m_ctx.commands.CanRedo() ? 0 : ui::U32(ui::kTextFaint)) && m_ctx.commands.CanRedo()) m_ctx.commands.Redo();
        ImGui::SameLine(0, 2);
        if (IconButton("##save", Icon::Save, m_ctx.scenePath.empty() ? "Save scene as... (Ctrl+S)" : "Save scene (Ctrl+S)", false, 28)) SaveSceneFile();

        // Play / Pause / Stop in the middle.
        {
            const float groupWidth = 3 * 34.0f + 2 * 6.0f;
            ImGui::SameLine();
            ImGui::SetCursorPosX((winWidth - groupWidth) * 0.5f);
            ImGui::SetCursorPosY(top);
            const bool playing = m_ctx.playing;
            struct Transport { const char* id; Icon icon; const char* tip; bool enabled; bool active; ImU32 color; };
            const Transport buttons[] = {
                {"##play", Icon::Play, "Play in editor (Alt+P)", !playing, playing, ui::U32(ui::kAccent)},
                {"##pause", Icon::Pause, "Pause / resume", playing, m_ctx.paused, ui::U32(ui::kTextSoft)},
                {"##stop", Icon::Stop, "Stop and restore the scene (Esc)", playing, false, ui::U32(ui::kTextSoft)},
            };
            for (size_t i = 0; i < 3; ++i) {
                if (i) ImGui::SameLine(0, 6);
                const Transport& t = buttons[i];
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const bool clicked = ImGui::InvisibleButton(t.id, ImVec2(34, 28));
                const bool hovered = ImGui::IsItemHovered();
                const ImVec2 end(pos.x + 34, pos.y + 28);
                dl->AddRectFilled(pos, end, ui::U32(t.active ? ui::kSelection : (hovered && t.enabled ? ui::kHover : ui::kBg)), 5.0f);
                dl->AddRect(pos, end, ui::U32(t.active ? ui::kAccent : ui::kBorder), 5.0f);
                DrawIcon(dl, t.icon, ImVec2(pos.x + 17, pos.y + 14), 14.0f, t.enabled || t.active ? t.color : ui::U32(ui::kTextFaint));
                if (hovered) ImGui::SetTooltip("%s", t.tip);
                if (!clicked || !t.enabled) continue;
                if (i == 0) m_ctx.playRequest = EditorContext::PlayRequest::Play;
                if (i == 1) m_ctx.paused = !m_ctx.paused;
                if (i == 2) m_ctx.playRequest = EditorContext::PlayRequest::Stop;
            }
        }

        // Right side: build state chips and the rebuild button.
        {
            const std::string& game = m_ctx.gameStatus;
            const bool failed = game.find("FAILED") != std::string::npos;
            const bool building = game.find("building") != std::string::npos;
            const bool ready = game == "Game: ready";
            const char* cppLabel = failed ? "C++ build failed" : (building ? "C++ building" : (ready ? "C++ built" : "C++ not built"));
            const ImVec4 cppDot = failed ? ui::kRed : (building ? ui::kYellow : (ready ? ui::kGreen : ui::kTextFaint));
            const char* luaLabel = "Lua hot reload";
            const float rebuildWidth = ImGui::CalcTextSize("Rebuild").x + 20.0f;
            float x = winPos.x + winWidth - 10.0f - rebuildWidth;
            const float y = winPos.y + top + 2;

            ImGui::SetCursorScreenPos(ImVec2(x, winPos.y + top));
            if (ToolbarButton("Rebuild")) m_builder.RequestBuild();
            x -= 8.0f + ChipWidth(luaLabel);
            DrawChip(ImVec2(x, y), luaLabel, ui::kGreen);
            x -= 8.0f + ChipWidth(cppLabel);
            DrawChip(ImVec2(x, y), cppLabel, cppDot);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// The thin bar at the bottom: scene, selection, game build state, frame rate.
void EditorApp::DrawStatusBar() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::kBase);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginViewportSideBar("##StatusBar", vp, ImGuiDir_Down, 24.0f, flags)) {
        ImGui::PushFont(GetFont(UiFont::Mono));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y), ui::U32(ui::kBorder));

        const std::string scene = m_ctx.scenePath.empty() ? "Untitled" : fs::path(m_ctx.scenePath).filename().string();
        std::string left = scene + (m_ctx.IsDirty() ? " *" : "");
        if (const NameComponent* name = m_ctx.scene.TryGet<NameComponent>(m_ctx.selected)) left += "  \xC2\xB7  " + name->name;
        left += "  \xC2\xB7  " + std::to_string(m_ctx.scene.Order().size()) + " actor(s)";
        ImGui::TextDisabled("%s", left.c_str());

        char right[96];
        std::snprintf(right, sizeof(right), "%s  \xC2\xB7  %.0f FPS", m_ctx.gameStatus.c_str(), ImGui::GetIO().Framerate);
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(right).x - 12.0f);
        ImGui::TextDisabled("%s", right);
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void EditorApp::BuildDefaultLayout(unsigned int dockId, float width, float height) {
    m_focusViewport = true;
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImVec2(width, height));

    ImGuiID center = dockId;
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.14f, nullptr, &center);
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.20f, nullptr, &center);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.27f, nullptr, &center);
    ImGuiID rightBottom = 0;
    const ImGuiID rightTop = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.42f, nullptr, &rightBottom);

    ImGui::DockBuilderDockWindow("Place Actors", left);
    ImGui::DockBuilderDockWindow("Outliner", rightTop);
    ImGui::DockBuilderDockWindow("Details", rightBottom);
    ImGui::DockBuilderDockWindow("Material", rightBottom);
    ImGui::DockBuilderDockWindow("Content Browser", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderDockWindow("Material Graph", center);
    ImGui::DockBuilderFinish(dockId);
}

// ---- Console -----------------------------------------------------------------------------------

void EditorApp::ExecuteCommand(const std::string& line) {
    Log::Info("> {}", line);
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;

    if (cmd == "help") {
        Log::Info("Commands:");
        Log::Info("  spawn <empty|cube|sphere|plane|cylinder|capsule|torus|stairs|light ...>   add an actor (try `spawn` for the full list)");
        Log::Info("  select <name> | delete | duplicate | focus | list | pick <ndcX> <ndcY>");
        Log::Info("  set <Component>.<Property> <value...>   e.g. set Transform.Position 1 2 3");
        Log::Info("                                          (use _ for spaces: Directional_Light.Intensity)");
        Log::Info("  undo | redo");
        Log::Info("  inspect <material> | matset <param> <value...> | browse <folder>   materials and Content Browser");
        Log::Info("  new [force] | open <path> [force] | save | saveas <path>   (force discards unsaved changes)");
        Log::Info("  screenshot [viewport|window] [path]     save a PNG (default: Saved/Screenshots)");
        Log::Info("  play | stop | build | game | addcomp <Name> | get <Component>.<Property>");
        Log::Info("  shader reload | clear | quit [force] | stats   (stats: frame and GPU time per pass over the viewport)");
        Log::Info("  mcp start|stop|status|config|copy claude|copy json   the control channel and server for AI clients (docs/MCP.md)");
        Log::Info("  camera [x y z [yaw pitch]] | lookat x y z | view 2d [x y [half]] | view 3d   the editor camera");
        Log::Info("  key <name> down|up|tap | mouse <x> <y> [down|up] | pause | resume   drive a playing game without a keyboard");
        Log::Info("  ui [tree] | ui <lua> | lua <code>       the UI tree; run Lua in the UI preview or (lua) in the running game");
        Log::Info("  plugins [reload]                        list plugins (Plugins/<name>/plugin.lua) or load them again");
        Log::Info("  (scripts for --exec only: wait <frames> | waitbuild | mouse <x> <y> [down|up] | wheel <x> <y> <notches>)");
    } else if (cmd == "clear") {
        Log::Clear();
    } else if (cmd == "spawn") {
        std::string what;
        in >> what;
        what = Lower(what);
        SpawnKind kind;
        if (FindSpawnKind(what, kind)) {
            SpawnActor(m_ctx, kind);
        } else {
            std::string words;
            for (int i = 0; i < static_cast<int>(SpawnKind::Count); ++i) {
                words += (i ? "|" : "") + std::string(GetSpawnInfo(static_cast<SpawnKind>(i)).command);
            }
            Log::Warn("Usage: spawn <{}>", words);
        }
    } else if (cmd == "delete") {
        DeleteActor(m_ctx, m_ctx.selected);
    } else if (cmd == "duplicate") {
        DuplicateActor(m_ctx, m_ctx.selected);
    } else if (cmd == "undo") {
        if (!m_ctx.commands.Undo()) Log::Info("Nothing to undo");
    } else if (cmd == "redo") {
        if (!m_ctx.commands.Redo()) Log::Info("Nothing to redo");
    } else if (cmd == "focus") {
        m_viewport.FocusSelection(m_ctx);
    } else if (cmd == "new" || cmd == "open") {
        std::string path, force;
        if (cmd == "open") in >> path;
        in >> force;
        if (cmd == "open" && path.empty()) {
            Log::Warn("Usage: open <path> [force]");
        } else if (m_ctx.IsDirty() && force != "force") {
            Log::Warn("The scene has unsaved changes. Run 'save' first, or add 'force' to discard them.");
        } else if (cmd == "new") {
            NewScene();
        } else {
            OpenSceneFile(path);
        }
    } else if (cmd == "save") {
        if (m_ctx.scenePath.empty()) Log::Warn("The scene is untitled. Use: saveas <path>");
        else SaveSceneFile();
    } else if (cmd == "saveas") {
        std::string path;
        in >> path;
        path = NormalizeScenePath(path);
        if (path.empty()) Log::Warn("Usage: saveas <path>");
        else SaveSceneAs(path);
    } else if (cmd == "panel") {
        std::string name;
        std::getline(in, name);
        ImGui::SetWindowFocus(Trim(name).c_str()); // brings a docked tab to the front
    } else if (cmd == "browse") {
        std::string folder;
        in >> folder;
        m_contentBrowser.ShowFolder(folder.empty() ? "Content" : folder);
    } else if (cmd == "inspect") {
        std::string path;
        in >> path;
        m_ctx.inspectedMaterial = path;
    } else if (cmd == "matset") {
        // matset <param> <number(s) | texture path>: edits the inspected (or selected actor's) material through undo.
        std::string path = m_ctx.inspectedMaterial;
        if (path.empty()) {
            if (const MeshComponent* mesh = m_ctx.scene.TryGet<MeshComponent>(m_ctx.selected)) path = mesh->material;
        }
        MaterialAsset* material = path.empty() ? nullptr : m_ctx.materials.Get(path);
        ShaderAsset* shader = material ? m_ctx.shaders.Get(material->Data().shader) : nullptr;
        std::string param;
        in >> param;
        const ShaderParam* sp = nullptr;
        if (shader) {
            for (const ShaderParam& p : shader->Params()) {
                if (p.name == param) sp = &p;
            }
        }
        if (!sp) {
            Log::Warn("Usage: matset <param> <value...> (needs a material and one of its shader's parameters)");
        } else {
            const MaterialData before = material->Data();
            MaterialData after = before;
            if (sp->isTexture) {
                std::string texture;
                in >> texture;
                after.SetTexture(param, texture == "none" ? std::string() : texture);
            } else {
                std::array<float, 4> v{};
                bool ok = true;
                for (int i = 0; i < sp->components; ++i) ok = ok && static_cast<bool>(in >> v[static_cast<size_t>(i)]);
                if (ok) after.SetNumber(param, sp->components, v);
                else Log::Warn("'{}' needs {} number(s)", param, sp->components);
            }
            material->SetData(after);
            RecordMaterialEdit(m_ctx, path, before, after);
        }
    } else if (cmd == "pick") {
        // Same path as a viewport click: ndc x/y in [-1, 1], y up.
        float x = 0.0f, y = 0.0f;
        if (in >> x >> y) {
            const Framebuffer& fb = m_viewport.Fb();
            const float aspect = static_cast<float>(fb.Width()) / static_cast<float>(std::max(fb.Height(), 1));
            m_ctx.selected = PickActor(m_ctx.scene, m_ctx.camera.ScreenRay({x, y}, aspect));
            const NameComponent* n = m_ctx.scene.TryGet<NameComponent>(m_ctx.selected);
            Log::Info("Picked: {}", n ? n->name : std::string("(nothing)"));
        } else {
            Log::Warn("Usage: pick <ndcX> <ndcY>");
        }
    } else if (cmd == "list") {
        for (const uint64_t id : m_ctx.scene.Order()) {
            Log::Info("  {}{}", m_ctx.scene.TryGet<NameComponent>(id)->name, id == m_ctx.selected ? "  (selected)" : "");
        }
    } else if (cmd == "select") {
        std::string rest;
        std::getline(in, rest);
        rest = Trim(rest);
        m_ctx.selected = 0;
        for (const uint64_t id : m_ctx.scene.Order()) {
            if (m_ctx.scene.TryGet<NameComponent>(id)->name == rest) m_ctx.selected = id;
        }
        if (!m_ctx.selected) Log::Warn("No actor named '{}'", rest);
    } else if (cmd == "set") {
        std::string target;
        in >> target;
        const size_t dot = target.find('.');
        if (!m_ctx.selected) {
            Log::Warn("Select an actor first");
        } else if (dot == std::string::npos) {
            Log::Warn("Usage: set <Component>.<Property> <value...>");
        } else {
            std::string compName = target.substr(0, dot);
            std::replace(compName.begin(), compName.end(), '_', ' ');
            const std::string propName = target.substr(dot + 1);
            const ComponentInfo* info = nullptr;
            for (const ComponentInfo* c : AllComponentInfos()) {
                if (Lower(c->name) == Lower(compName)) info = c;
            }
            const entt::entity e = m_ctx.scene.Find(m_ctx.selected);
            if (!info || !info->has(m_ctx.scene.Registry(), e)) {
                Log::Warn("The selected actor has no component '{}'", compName);
            } else {
                bool found = false;
                for (size_t i = 0; i < info->props.size(); ++i) {
                    if (Lower(info->props[i].name) != Lower(propName)) continue;
                    found = true;
                    PropertyValue value;
                    if (ParsePropertyValue(info->props[i], in, value)) {
                        SetPropertyCommand(m_ctx, m_ctx.selected, *info, i, value);
                    } else {
                        Log::Warn("Could not read a value for {}.{}", info->name, info->props[i].name);
                    }
                }
                if (!found) Log::Warn("{} has no property '{}'", info->name, propName);
            }
        }
    } else if (cmd == "shader") {
        std::string sub;
        in >> sub;
        if (sub == "reload") {
            m_ctx.shaders.ReloadAll();
            m_ctx.materials.ReloadAll();
        } else {
            Log::Warn("Usage: shader reload");
        }
    } else if (cmd == "screenshot") {
        std::string target = "viewport", path;
        in >> target >> path;
        RequestScreenshot(target, path);
    } else if (cmd == "icons") {
        m_showIconSheet = !m_showIconSheet;
    } else if (cmd == "graph") {
        // graph new|open <shader.lua> | add <type> [x y] | link <src> <dst> <pin> | unlink <dst> <pin>
        //       | pin <node> <pin> <value> | delete <id> | list
        std::string sub;
        in >> sub;
        if (sub == "new" || sub == "open") {
            std::string path;
            in >> path;
            if (path.empty()) Log::Warn("Usage: graph {} <Content/Shaders/name.lua>", sub);
            else if (sub == "new") m_graphPanel.CreateNew(m_ctx, path);
            else m_graphPanel.Open(m_ctx, path);
        } else if (sub == "wheel") { // graph wheel -3: zoom out three notches, as the mouse wheel does
            float notches = 0.0f;
            in >> notches;
            m_graphPanel.Wheel(notches);
        } else if (sub == "zoom") {
            float zoom = 1.0f;
            in >> zoom;
            m_graphPanel.SetZoom(zoom);
        } else if (sub == "add") {
            std::string type;
            float x = 400.0f, y = 400.0f;
            in >> type >> x >> y;
            const int id = m_graphPanel.AddNodeByName(type, x, y);
            if (id) Log::Info("Added node {} ({})", id, type);
            else Log::Warn("Unknown node type '{}' (or no graph is open)", type);
        } else if (sub == "link") {
            int src = 0, dst = 0, pin = 0;
            if (in >> src >> dst >> pin) m_graphPanel.Link(src, dst, pin);
            else Log::Warn("Usage: graph link <source id> <destination id> <pin>");
        } else if (sub == "unlink") {
            int dst = 0, pin = 0;
            if (in >> dst >> pin) m_graphPanel.Unlink(dst, pin);
            else Log::Warn("Usage: graph unlink <destination id> <pin>");
        } else if (sub == "pin") {
            int node = 0, pin = 0;
            double value = 0.0;
            if (in >> node >> pin >> value) m_graphPanel.SetPinConstant(node, pin, value);
            else Log::Warn("Usage: graph pin <node id> <pin> <number>");
        } else if (sub == "delete") {
            int id = 0;
            if (!(in >> id) || !m_graphPanel.DeleteNode(id)) Log::Warn("Usage: graph delete <node id>");
        } else if (sub == "list") {
            m_graphPanel.LogGraph();
        } else {
            Log::Warn("Usage: graph new|open|add|link|unlink|pin|delete|list");
        }
    } else if (cmd == "play") {
        m_ctx.playRequest = EditorContext::PlayRequest::Play;
    } else if (cmd == "stop") {
        m_ctx.playRequest = EditorContext::PlayRequest::Stop;
    } else if (cmd == "build") {
        m_builder.RequestBuild();
    } else if (cmd == "game") {
        Log::Info("{}; module {} (version {})", m_ctx.gameStatus, m_game.IsLoaded() ? "loaded" : "not loaded", m_game.Generation());
        for (const std::string& n : m_game.ComponentNames()) Log::Info("  component {}", n);
    } else if (cmd == "addcomp") {
        std::string name;
        std::getline(in, name);
        name = Trim(name);
        std::replace(name.begin(), name.end(), '_', ' ');
        const ComponentInfo* info = nullptr;
        for (const ComponentInfo* c : AllComponentInfos()) {
            if (Lower(c->name) == Lower(name)) info = c;
        }
        const entt::entity e = m_ctx.scene.Find(m_ctx.selected);
        if (!info || info->hidden || !info->removable) Log::Warn("Unknown component '{}' (use 'game' to list the game's)", name);
        else if (e == entt::null) Log::Warn("Select an actor first");
        else if (info->has(m_ctx.scene.Registry(), e)) Log::Warn("The actor already has {}", info->name);
        else AddComponentCommand(m_ctx, m_ctx.selected, *info);
    } else if (cmd == "get") {
        // get <Component>.<Property>: prints a value of the selected actor
        std::string target;
        in >> target;
        const size_t dot = target.find('.');
        const entt::entity e = m_ctx.scene.Find(m_ctx.selected);
        if (dot == std::string::npos || e == entt::null) {
            Log::Warn("Usage: get <Component>.<Property> (with an actor selected)");
        } else {
            std::string compName = target.substr(0, dot);
            std::replace(compName.begin(), compName.end(), '_', ' ');
            const std::string propName = target.substr(dot + 1);
            bool found = false;
            for (const ComponentInfo* c : AllComponentInfos()) {
                if (Lower(c->name) != Lower(compName) || !c->has(m_ctx.scene.Registry(), e)) continue;
                for (const PropertyInfo& p : c->props) {
                    if (Lower(p.name) != Lower(propName)) continue;
                    found = true;
                    const PropertyValue v = GetProperty(p, c->get(m_ctx.scene.Registry(), e));
                    std::string text;
                    if (const bool* b = std::get_if<bool>(&v)) text = *b ? "true" : "false";
                    else if (const int* i = std::get_if<int>(&v)) text = p.kind == PropKind::Enum && *i >= 0 && static_cast<size_t>(*i) < p.options.size() ? p.options[static_cast<size_t>(*i)] : std::to_string(*i);
                    else if (const float* f = std::get_if<float>(&v)) text = std::format("{:.4g}", *f);
                    else if (const glm::vec3* g = std::get_if<glm::vec3>(&v)) text = std::format("{:.4g} {:.4g} {:.4g}", g->x, g->y, g->z);
                    else text = std::get<std::string>(v);
                    Log::Info("{}.{} = {}", c->name, p.name, text);
                }
            }
            if (!found) Log::Warn("The selected actor has no {}.{}", compName, propName);
        }
    } else if (cmd == "wait") {
        in >> m_scriptWait; // pauses a --exec script for N frames
    } else if (cmd == "waitbuild") {
        m_scriptWaitBuild = true; // pauses a --exec script until the game build (and its reload) is finished
    } else if (cmd == "quit") {
        std::string force;
        in >> force;
        if (force == "force") m_forceQuit = true; // scripted runs: do not stop to ask about unsaved changes
        m_window.RequestClose();
    } else if (cmd == "wheel") { // wheel <x> <y> <notches>: puts the mouse at a window position and turns the wheel (for scripted tests)
        float x = 0.0f, y = 0.0f, notches = 0.0f;
        in >> x >> y >> notches;
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        io.AddMouseWheelEvent(0.0f, notches);
    } else if (cmd == "mouse") { // mouse <x> <y> [down|up] | mouse off: holds the mouse at a window position and presses or releases the left button (scripted tests)
        float x = 0.0f, y = 0.0f;
        std::string state;
        in >> x >> y >> state;
        if (state == "off") { // give the mouse back to the real cursor
            m_scriptMouse.active = false;
        } else {
            m_scriptMouse.active = true;
            // With several platform windows ImGui's mouse is in screen coordinates: window pixels are relative to the main window.
            const ImVec2 origin = ImGui::GetMainViewport()->Pos;
            m_scriptMouse.x = origin.x + x;
            m_scriptMouse.y = origin.y + y;
            Log::Info("mouse at window ({:.0f}, {:.0f}) = screen ({:.0f}, {:.0f})", x, y, m_scriptMouse.x, m_scriptMouse.y);
            if (state == "down") m_scriptMouse.down = true;
            else if (state == "up") m_scriptMouse.down = false;
        }
    } else if (cmd == "view") { // view 2d [x y [half_height]] | view 3d: the viewport's flat 2D mode
        std::string mode;
        in >> mode;
        m_viewport.SetView2D(Lower(mode) == "2d");
        float x = 0.0f, y = 0.0f, half = 0.0f;
        if (Lower(mode) == "2d" && (in >> x >> y)) {
            if (!(in >> half) || half <= 0.0f) half = m_viewport.Half2D();
            m_viewport.SetCenter2D({x, y}, half);
        }
    } else if (cmd == "camera") { // camera <x> <y> <z> [yaw pitch]: the editor camera (3D view); without angles it keeps looking where it did
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (in >> x >> y >> z) {
            m_ctx.camera.position = {x, y, z};
            float yaw = 0.0f, pitch = 0.0f;
            if (in >> yaw >> pitch) {
                m_ctx.camera.yaw = yaw;
                m_ctx.camera.pitch = std::clamp(pitch, -89.0f, 89.0f);
            }
        } else {
            const EditorCamera& c = m_ctx.camera;
            Log::Info("camera {:.3f} {:.3f} {:.3f}  yaw {:.2f} pitch {:.2f}", c.position.x, c.position.y, c.position.z, c.yaw, c.pitch);
        }
    } else if (cmd == "lookat") { // lookat <x> <y> <z>: turns the editor camera towards a point
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (in >> x >> y >> z) m_ctx.camera.LookAt({x, y, z});
        else Log::Warn("Usage: lookat <x> <y> <z>");
    } else if (cmd == "key") { // key <name> down | up | tap: presses a game key without a keyboard (scripted runs, tools)
        std::string name, state = "tap";
        in >> name >> state;
        const int code = Gameplay::KeyCodeFromName(name);
        if (code < 0) {
            Log::Warn("Unknown key '{}' (a-z, 0-9, space, shift, ctrl, alt, tab, enter, escape, up, down, left, right)", name);
        } else if (state == "down") {
            m_scriptKeys[code] = -1;
        } else if (state == "up") {
            m_scriptKeys.erase(code);
        } else if (state == "tap") {
            m_scriptKeys[code] = 4; // held for four frames, so the game sees both the press and the release
        } else {
            Log::Warn("Usage: key <name> down|up|tap");
        }
    } else if (cmd == "pause" || cmd == "resume") { // pause / resume: the Pause button of the toolbar
        if (m_ctx.playing) m_ctx.paused = cmd == "pause";
        else Log::Warn("Nothing is playing");
    } else if (cmd == "ui") { // ui tree | ui <lua>: the UI of the running game, or the preview of the scene's canvases
        std::string rest;
        std::getline(in, rest);
        rest = Trim(rest);
        UiSystem& active = m_ctx.playing ? m_gameplay.Ui() : m_uiPreview.System();
        if (rest.compare(0, 7, "select ") == 0) { // ui select <node name>
            const UiId found = active.Find(Trim(rest.substr(7)));
            if (found) m_ctx.uiSelected = found;
            else Log::Warn("No UI node named '{}'", Trim(rest.substr(7)));
        } else if (rest.empty() || rest == "tree") {
            const std::string dump = active.Dump();
            if (dump.empty()) Log::Info("The UI is empty: add a Canvas actor (Place Actors > UI) with a document, or run: ui <lua>");
            std::istringstream lines(dump);
            for (std::string l; std::getline(lines, l);) Log::Info("{}", l);
        } else {
            std::string error;
            const bool ok = m_ctx.playing ? m_gameplay.RunLua(rest, error) : m_uiPreview.Run(rest, error);
            if (!ok) Log::Error("ui: {}", error);
        }
    } else if (cmd == "mcp") { // mcp start | stop | status | config | copy claude|json: the control channel for AI clients
        std::string rest;
        std::getline(in, rest);
        McpCommand(Trim(rest));
    } else if (cmd == "lua") { // lua <code>: runs in the running game's script VM
        std::string rest;
        std::getline(in, rest);
        std::string error;
        if (!m_gameplay.RunLua(Trim(rest), error)) Log::Error("lua: {}", error);
    } else if (cmd == "stats") {
        m_viewport.ToggleStats(); // frame time and GPU time per pass over the viewport
    } else if (cmd == "plugins") {
        for (const PluginHost::Info& p : m_plugins.Plugins()) {
            Log::Info("  {} - {}", p.name, p.error.empty() ? std::to_string(p.menus) + " menu item(s), " + std::to_string(p.commands) +
                                                                " command(s), " + std::to_string(p.panels) + " panel(s)"
                                                          : "FAILED: " + p.error);
        }
        std::string sub;
        in >> sub;
        if (sub == "reload") m_plugins.Reload();
        else if (sub == "open") { // plugins open <name> [x y]: shows a plugin's window
            std::string name;
            float x = 320.0f, y = 140.0f;
            in >> name >> x >> y;
            if (!m_plugins.OpenPanel(name, x, y)) Log::Warn("No plugin window called '{}'", name);
        }
    } else if (std::string rest; (std::getline(in, rest), m_plugins.RunCommand(cmd, Trim(rest)))) {
        // handled by a plugin
    } else {
        Log::Warn("Unknown command '{}'. Type 'help'.", cmd);
    }
}

void EditorApp::PumpScript() {
    if (m_frame < 3) return; // let the first frames settle (layout, first shader compiles)
    if (m_scriptWait > 0) {
        --m_scriptWait;
        return;
    }
    if (m_scriptWaitBuild) {
        if (m_builder.Pending()) return;
        m_scriptWaitBuild = false;
    }
    while (!m_script.empty() && m_scriptWait == 0 && !m_scriptWaitBuild) {
        const std::string line = m_script.front();
        m_script.pop_front();
        ExecuteCommand(line); // may set m_scriptWait / m_scriptWaitBuild
        if (m_scriptWait > 0) return;
    }
}

void EditorApp::RequestScreenshot(const std::string& target, const std::string& path) {
    if (target != "viewport" && target != "window") {
        Log::Warn("Unknown screenshot target '{}', use 'viewport' or 'window'", target);
        return;
    }
    m_pendingShotTarget = target;
    m_pendingShotPath = path.empty() ? TimestampedShotPath(target) : path;
}

void EditorApp::ProcessScreenshot() {
    // CLI mode: capture once the layout has settled, then quit.
    if (!m_options.shotPath.empty() && m_frame == m_options.shotFrames) {
        RequestScreenshot(m_options.shotTarget, m_options.shotPath);
    }

    if (!m_pendingShotTarget.empty()) {
        if (m_pendingShotTarget == "viewport") {
            Framebuffer& fb = m_viewport.Fb();
            SavePng(m_pendingShotPath, fb.Width(), fb.Height(), fb.ReadPixels());
        } else {
            int w = 0, h = 0;
            m_window.FramebufferSize(w, h);
            SaveBackBufferPng(m_pendingShotPath, w, h);
        }
        m_pendingShotTarget.clear();
        if (!m_options.shotPath.empty()) {
            m_forceQuit = true; // scripted run: never stop to ask about unsaved changes
            m_window.RequestClose();
        }
    }
}

} // namespace magnum::editor
