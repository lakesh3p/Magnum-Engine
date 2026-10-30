#pragma once

#include "ControlServer.h"
#include "EditorContext.h"
#include "PluginHost.h"
#include "panels/ConsolePanel.h"
#include "panels/ContentBrowserPanel.h"
#include "panels/DetailsPanel.h"
#include "panels/OutlinerPanel.h"
#include "panels/PlaceActorsPanel.h"
#include "panels/UiPanel.h"
#include "panels/MaterialGraphPanel.h"
#include "panels/MaterialPanel.h"
#include "panels/ViewportPanel.h"
#include "game/GameBuilder.h"
#include "game/GameRuntime.h"
#include "game/Gameplay.h"
#include "ui/UiPreview.h"
#include "platform/Window.h"

#include <chrono>
#include <deque>
#include <map>
#include <filesystem>
#include <vector>
#include <string>

namespace magnum::editor {

struct EditorOptions {
    // Feedback loop for scripted runs: save a PNG after N frames and exit.
    std::string shotPath;
    std::string shotTarget = "window"; // "window" or "viewport"
    int shotFrames = 12;
    // Console commands (separated by ';') run on the third frame, before the screenshot.
    std::string exec;
    // The project folder (the one with the .magnum file). Empty: the last project, or the MagnumDemo sample.
    std::string project;
    // Opens the local control channel (see ControlServer.h): -1 off, 0 any free port, else that port.
    int controlPort = -1;
};

class EditorApp {
public:
    bool Init(const EditorOptions& options);
    void Run();
    void Shutdown();

private:
    enum class PendingAction { None, NewScene, OpenScene, Quit, SwitchProject };

    void BeginFrame();
    void EndFrame();
    void DrawMainMenu();
    void DrawMainToolbar();
    void DrawStatusBar();
    void DrawDockHost();
    void BuildDefaultLayout(unsigned int dockId, float width, float height);
    void HandleShortcuts();
    void BuildDefaultScene();

    // Scene files. Actions that would discard unsaved changes go through RequestAction first.
    void NewScene();
    bool OpenSceneFile(const std::string& relPath);
    bool SaveSceneFile();
    bool SaveSceneAs(const std::string& relPath);
    void RequestAction(PendingAction action, const std::string& path = {});
    void PerformPending();
    void DrawSceneDialogs();
    void UpdateWindowTitle();

    // Projects live in folders of their own; opening another one starts a fresh editor for it.
    void NewProjectDialog();
    void OpenProjectDialog();
    void RequestOpenProject(const std::filesystem::path& folder);

    // Game module and play-in-editor.
    void ReloadGameModule();
    void UpdateGame();
    void StartPlay();
    void StopPlay();

    void ExecuteCommand(const std::string& line);
    void PumpScript();
    void PumpControl();                      // answers the requests of the control channel (EditorControl.cpp)
    void HandleControl(const nlohmann::json& request);
    void FinishControl();
    void McpCommand(const std::string& args);
    std::string McpStatus() const;
    McpInfo McpInfoNow() const;
    void RequestScreenshot(const std::string& target, const std::string& path);
    void ProcessScreenshot();

    EditorOptions m_options;
    Window m_window;
    EditorContext m_ctx;

    ViewportPanel m_viewport;
    OutlinerPanel m_outliner;
    DetailsPanel m_details;
    UiPanel m_uiPanel;
    struct ScriptMouse {
        bool active = false, down = false;
        float x = 0.0f, y = 0.0f;
    } m_scriptMouse; // a pointer driven by the `mouse` console command (scripted runs)
    std::map<int, int> m_scriptKeys; // keys held by the `key` console command: GLFW code -> frames left (-1 = until released)
    PlaceActorsPanel m_placeActors;
    MaterialPanel m_materialPanel;
    MaterialGraphPanel m_graphPanel;
    ContentBrowserPanel m_contentBrowser;
    ConsolePanel m_console;
    PluginHost m_plugins;

    std::string m_imguiIniPath;
    bool m_resetLayout = false;
    int m_frame = 0;
    bool m_showIconSheet = false;
    bool m_focusViewport = false;
    uint64_t m_lastSelected = 0;

    PendingAction m_pending = PendingAction::None;
    std::string m_pendingPath;
    bool m_openUnsavedPopup = false;
    bool m_openSaveAsPopup = false;
    bool m_runPendingAfterSave = false;
    bool m_forceQuit = false;
    char m_saveAsBuffer[256] = {};
    std::string m_windowTitle;
    std::string m_projectName;
    std::string m_defaultScene = "Content/Scenes/Default.scene";
    std::vector<std::filesystem::path> m_recentProjects;

    std::string m_pendingShotTarget;
    std::string m_pendingShotPath;

    GameRuntime m_game;
    Gameplay m_gameplay; // scripts, physics and the player camera while playing
    UiPreview m_uiPreview; // the scene's Canvas components shown while nothing plays
    GameBuilder m_builder;
    std::string m_playBackup; // the scene as it was when play started (JSON text)
    std::filesystem::path m_gameDll;

    // Commands from --exec, run one frame at a time so `wait` and `waitbuild` can pause between them.
    std::deque<std::string> m_script;
    int m_scriptWait = 0;
    bool m_scriptWaitBuild = false;

    // The control channel: one request at a time; a request that needs frames to pass is answered when they did.
    ControlServer m_control;
    struct ControlPending {
        bool active = false;
        nlohmann::json id;
        int frames = 0;
        uint64_t logMark = 0;
        bool collectLog = false;
        bool screenshot = false;
        bool build = false;
        bool quit = false;
        std::string path;
        std::string error; // a failure that is reported next to the output
        std::chrono::steady_clock::time_point until{}; // do not answer before this moment (file watchers poll in real time)
    } m_controlPending;
};

} // namespace magnum::editor
