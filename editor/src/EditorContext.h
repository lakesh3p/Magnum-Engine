#pragma once

#include "EditorCamera.h"
#include "ThumbnailCache.h"
#include "commands/CommandStack.h"
#include "material/MaterialLibrary.h"
#include "platform/Window.h"
#include "game/Gameplay.h"
#include "render/SceneRenderer.h"
#include "render/ShaderLibrary.h"
#include "render/Texture.h"
#include "scene/Scene.h"
#include "ui/UiDraw.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace magnum::editor {

// The viewport tool: Select only picks, the others also show a gizmo on the selected actor.
enum class GizmoMode { Select, Translate, Rotate, Scale };

// Everything the editor panels share.
struct EditorContext {
    Window* window = nullptr;
    std::filesystem::path projectRoot;
    Scene scene;
    CommandStack commands;
    EditorCamera camera;
    uint64_t selected = 0; // actor id, 0 = nothing
    GizmoMode gizmoMode = GizmoMode::Translate;
    bool view2D = false;          // the viewport shows the flat 2D view, so new actors go to its middle
    glm::vec2 view2DCenter{0.0f};
    bool gizmoLocal = false; // gizmo space: local axes instead of world axes
    bool gizmoSnap = false;  // snap to grid while dragging (Ctrl flips it)
    float deltaTime = 0.0f;
    double time = 0.0;

    // Assets.
    ShaderLibrary shaders;
    MaterialLibrary materials;
    TextureLibrary textures;
    SceneRenderer renderer;
    ThumbnailCache thumbnails;
    RenderPipeline pipeline; // the frame's pass list, from Content/Pipelines/default.lua
    RenderResources Resources() { return {&shaders, &materials, &textures, &pipeline}; }

    // Play-in-editor state, owned by EditorApp. Panels only read `playing` and ask for changes via playRequest.
    enum class PlayRequest { None, Play, Stop };
    bool playing = false;
    bool paused = false;                // the game stops updating, the scene stays as it is
    PlayRequest playRequest = PlayRequest::None;
    bool gameInputEnabled = false;      // the viewport has focus and is not being flown: keys reach the game
    // While playing: the view the player sees (the scene's primary camera), what scripts draw over it, and the mouse.
    CameraView gameCamera;
    std::vector<HudItem> hud;
    bool mouseCaptured = false;         // the cursor is hidden and mouse movement goes to the game (Esc gives it back)
    glm::vec2 gameMouseDelta{0.0f};
    glm::vec2 gameMousePosition{0.5f};  // over the game view, 0..1 from the top left
    bool gameMouseInView = false;
    float gameAspect = 16.0f / 9.0f;
    glm::vec2 gameViewSize{1280.0f, 720.0f}; // the viewport in pixels, for the UI
    glm::vec2 gameViewOrigin{0.0f};          // its top left corner in main-window pixels (for the `mouse` command)
    float gameMouseWheel = 0.0f;
    // The UI over the viewport: while playing the game's, otherwise a preview of the scene's Canvas components.
    bool showUi = true;
    bool uiPointerOver = false;         // playing: the pointer is on a UI element that stops it
    UiSystem* activeUi = nullptr;       // the running game's UI while playing, else the preview
    UiId uiSelected = 0;                // the node picked in the UI panel (outlined in the viewport)
    unsigned int detailsDock = 0;       // dock node of the Details panel: the UI panel opens beside it
    UiDrawList uiDraw;
    std::function<void(int, int, UiDrawList&)> buildUiDraw; // lays the active UI out for a view size and draws it into the list
    std::string gameStatus = "Game: not built";

    // A graph shader (project-relative path) that another panel wants opened in the Material Graph editor.
    std::string graphRequest;

    // Material shown in the Material panel when the user picked one in the Content Browser ("" = the
    // selected actor's material).
    std::string inspectedMaterial;

    // The open scene file, relative to projectRoot ("" = untitled).
    std::string scenePath;
    uint64_t savedRevision = 0;
    bool IsDirty() const { return commands.Revision() != savedRevision; }

    // Asks the editor to open a scene (it handles unsaved changes first).
    std::function<void(const std::string&)> openScene;

    // Releases GL objects while the context is alive.
    void ShutdownGraphics() {
        thumbnails.Clear();
        materials.Clear();
        shaders.Clear();
        textures.Clear();
        renderer.Shutdown();
    }
};

} // namespace magnum::editor
